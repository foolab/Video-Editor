/* QDeclarativeVideoEditor
 * Copyright (C) 2012 Thiago Sousa Santos <thiago.sousa.santos@collabora.co.uk>
 * Copyright (C) 2012 Robert Swain <robert.swain@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "qdeclarativevideoeditor.h"
#include "videoeditoritem.h"

#include <QDebug>
#include <QDateTime>
#include <QFileInfo>

extern "C" {
    #include "gstcapstricks.h"
}

#define RENDERING_FAILED "Rendering failed"
#define PLAYBACK_FAILED "Playback failed"
#define NO_MEDIA "Add clips before exporting"

#define MOVIES_DIR "/home/user/MyDocs/Movies/"

#define PROGRESS_TIMEOUT (1000/30)

enum {
    OBJECT_ROLE = Qt::UserRole,
};

static gboolean bus_call(GstBus * bus, GstMessage * msg, gpointer data);
static GstBusSyncReply bus_sync_call(GstBus * bus, GstMessage * msg, gpointer data);

QDeclarativeVideoEditor::QDeclarativeVideoEditor(QObject *parent) :
    QAbstractListModel(parent), m_position(0), m_positionTimer(this), m_rendering(false), m_size(0),
    m_width(1280), m_height(720), m_fpsn(30), m_fpsd(1)
{
    QHash<int, QByteArray> roles;
    roles.insert(OBJECT_ROLE, "object" );
    setRoleNames(roles);

    connect(&m_positionTimer, SIGNAL(timeout()), SLOT(updatePosition()));

    m_timeline = ges_timeline_new_audio_video();
    m_timelineLayer = (GESTimelineLayer*) ges_simple_timeline_layer_new();
    ges_timeline_add_layer(m_timeline, m_timelineLayer);
    m_pipeline = ges_timeline_pipeline_new();

    GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (m_pipeline));
    gst_bus_add_watch (bus, bus_call, this);
    gst_bus_set_sync_handler(bus, bus_sync_call, this);
    gst_object_unref (bus);

    /*
     * gst-dsp encoders seems to not proxy downstream caps correctly, this can make
     * GES fail to render some projects. We override the default getcaps on our own
     */
    g_signal_connect(m_pipeline, "element-added", (GCallback) gstcapstricks_pipeline_element_added, NULL);
    {
        GstElement *playsink = gst_bin_get_by_name(GST_BIN(m_pipeline), "internal-sinks");
        if(playsink) {
            gstcapstricks_set_playsink_sendevent(playsink);
            gst_object_unref (playsink);
        } else {
            qDebug() << "No playsink found";
        }
    }

    ges_timeline_pipeline_add_timeline (m_pipeline, m_timeline);

    m_vsink = gst_element_factory_make ("omapxvsink", "previewvsink");
    ges_timeline_pipeline_preview_set_video_sink (m_pipeline, m_vsink);
    gst_x_overlay_set_render_rectangle (GST_X_OVERLAY (m_vsink),
                                        171, 0,
                                        512, 288);

    ges_timeline_pipeline_set_mode (m_pipeline, TIMELINE_MODE_PREVIEW);
    gst_element_set_state ((GstElement*) m_pipeline, GST_STATE_PAUSED);
    m_duration = GST_CLOCK_TIME_NONE;
    m_progress = 0.0;
}

QDeclarativeVideoEditor::~QDeclarativeVideoEditor()
{
    m_positionTimer.stop();
    gst_element_set_state ((GstElement*) m_pipeline, GST_STATE_NULL);
    gst_object_unref (m_vsink);
    gst_object_unref (m_pipeline);
}

int QDeclarativeVideoEditor::rowCount(const QModelIndex &parent) const
{
    return !parent.isValid() ? m_size : 0;
}

QVariant QDeclarativeVideoEditor::data(const QModelIndex &index, int role) const
{
    if (index.isValid() && index.row() < m_size) {
        QVariant ret = NULL;
        VideoEditorItem *item = m_items.at(index.row());
        switch (role) {
        case OBJECT_ROLE:
            ret = QVariant::fromValue<VideoEditorItem *>(item);
            break;
        default:
        {
            qDebug() << "Unknown role: " << role;
            break;
        }
        }
        return ret;
    } else {
        return QVariant();
    }
}

void timeline_filesource_maxduration_cb (GObject *, GParamSpec *, gpointer user_data)
{
    VideoEditorItem *item = (VideoEditorItem *)user_data;

    quint64 dur = GST_CLOCK_TIME_NONE;
    g_object_get (item->getTlfs(), "max-duration", &dur, NULL);
    if (item->getMaxDuration() == -1)
        item->setMaxDuration(dur);
    item->setDuration(dur);
}

void QDeclarativeVideoEditor::objectUpdated(VideoEditorItem *item)
{
    updateDuration();

    int row = m_items.indexOf(item);
    emit dataChanged(index(row), index(row));
}

bool QDeclarativeVideoEditor::append(const QString &value)
{
    qDebug() << "Appending new item:" << value;
    beginInsertRows(QModelIndex(), rowCount(), rowCount());

    VideoEditorItem *item = new VideoEditorItem();

    item->setTlfs(ges_timeline_filesource_new(value.toUtf8().data()));
    item->setUri(value);
    QFileInfo file(value.toUtf8().data());
    item->setFileName(file.fileName());
    item->setDurHdlrID(g_signal_connect(item->getTlfs(), "notify::max-duration",
                       G_CALLBACK(timeline_filesource_maxduration_cb), item));

    /* if the inpoint, duration, or max-duration change, we need to update m_duration */
    connect(item, SIGNAL(inPointChanged(VideoEditorItem*)),
            SLOT(objectUpdated(VideoEditorItem*)));
    connect(item, SIGNAL(durationChanged(VideoEditorItem*)),
            SLOT(objectUpdated(VideoEditorItem*)));
    connect(item, SIGNAL(maxDurationChanged(VideoEditorItem*)),
            SLOT(objectUpdated(VideoEditorItem*)));

    m_items.append(item);

    bool r = ges_timeline_layer_add_object(m_timelineLayer, (GESTimelineObject*) item->getTlfs());
    if (r) m_size++;

    endInsertRows();
    return r;
}

QVariant QDeclarativeVideoEditor::getObjDuration(int idx)
{
    if (idx >= 0 && idx < rowCount()) {
        return data(index(idx), OBJECT_ROLE).value<VideoEditorItem *>()->getDuration();
    }
    return GST_SECOND; // one second for safety
}

void QDeclarativeVideoEditor::move(int from, int to)
{   
    int qlist_to = to;
    qDebug() << "Moving media object from " << from << " to " << to;

    if(to == -1) {
        qlist_to = m_items.size()-1;
    }

    beginResetModel();
    const VideoEditorItem *item = m_items.at(from);
    ges_simple_timeline_layer_move_object(GES_SIMPLE_TIMELINE_LAYER (m_timelineLayer),
                                          (GESTimelineObject *)item->getTlfs(), to);
    m_items.move(from, qlist_to);
    endResetModel();
}

void QDeclarativeVideoEditor::removeAt(int idx)
{
    beginRemoveRows(QModelIndex(), idx, idx);
    VideoEditorItem *item = m_items.takeAt(idx);
    ges_timeline_layer_remove_object(m_timelineLayer, (GESTimelineObject *)item->getTlfs());
    g_signal_handler_disconnect(item->getTlfs(), item->getDurHdlrID());
    delete item;
    m_size--;
    endRemoveRows();
    updateDuration();
}

void QDeclarativeVideoEditor::removeAll()
{
    beginRemoveRows(QModelIndex(), 0, rowCount());
    while(m_items.isEmpty() == false) {
        VideoEditorItem *item = m_items.takeFirst();
        ges_timeline_layer_remove_object(m_timelineLayer, (GESTimelineObject *)item->getTlfs());
        g_signal_handler_disconnect(item->getTlfs(), item->getDurHdlrID());
        delete item;
        m_size--;
    }
    endRemoveRows();

    setPosition(0);
    setProgress(0);
    gst_element_set_state (GST_ELEMENT (m_pipeline), GST_STATE_READY);
    gst_element_set_state (GST_ELEMENT (m_pipeline), GST_STATE_PAUSED);
    updateDuration();
    emit playingStateChanged();
}

GstEncodingProfile *QDeclarativeVideoEditor::createEncodingProfile() const {
    GstEncodingProfile *profile = (GstEncodingProfile *)
            gst_encoding_container_profile_new("mp4", NULL, gst_caps_new_simple("video/quicktime",
                                                                                "variant", G_TYPE_STRING, "iso",
                                                                                NULL), NULL);

    GstEncodingProfile *video = NULL;
    if (m_width > 0 && m_height > 0 && m_fpsn > 0 && m_fpsd > 0) {
        video = (GstEncodingProfile *)
            gst_encoding_video_profile_new(gst_caps_new_simple("video/mpeg", "mpegversion",
                                                               G_TYPE_INT, 4,
                                                               "width", G_TYPE_INT, m_width,
                                                               "height", G_TYPE_INT, m_height,
                                                               "framerate", GST_TYPE_FRACTION_RANGE,
                                                               m_fpsn-1, m_fpsd, m_fpsn+1, m_fpsd, NULL),
                                           NULL, NULL, 1);
    } else {
        video = (GstEncodingProfile *)
            gst_encoding_video_profile_new(gst_caps_new_simple("video/mpeg", "mpegversion",
                                                               G_TYPE_INT, 4, NULL), NULL, NULL, 1);
    }
    GstEncodingProfile *audio = (GstEncodingProfile *)
            gst_encoding_audio_profile_new(gst_caps_new_simple("audio/mpeg", "mpegversion",
                                                               G_TYPE_INT, 4,
                                                               "rate", G_TYPE_INT, 48000,
                                                               "channels", G_TYPE_INT, 2, NULL), NULL, NULL, 0);

    gst_encoding_container_profile_add_profile((GstEncodingContainerProfile*) profile, video);
    gst_encoding_container_profile_add_profile((GstEncodingContainerProfile*) profile, audio);

    return profile;
}

gboolean
QDeclarativeVideoEditor::handleBusMessage (GstBus *, GstMessage *msg)
{
    switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
        qDebug() << "End of stream";
        setProgress(1.0);
        pause();
        if(isRendering()) {
            m_rendering = false;
            ges_timeline_pipeline_set_mode (m_pipeline, TIMELINE_MODE_PREVIEW);
            emit renderComplete();
        }
        setProgress(-1.0);
        break;

    case GST_MESSAGE_ERROR: {
        gchar  *debug;
        GError *gerror;

        gst_message_parse_error (msg, &gerror, &debug);
        g_free (debug);

        qDebug() << "Error: " << gerror->message;
        if(isRendering()) {
            m_rendering = false;
            emit error(RENDERING_FAILED, gerror->message);
        } else {
            emit error(PLAYBACK_FAILED, gerror->message);
        }
        g_error_free (gerror);
        pause();
        gst_element_set_state ((GstElement *) m_pipeline, GST_STATE_NULL);
        setProgress(-1.0);
        break;
    }
    case GST_MESSAGE_ASYNC_DONE:
    {
        qDebug() << "Async done, updating position";
        updatePosition();
    }
        break;
    default:
        break;
    }

    return TRUE;
}

GstBusSyncReply
QDeclarativeVideoEditor::handleSyncBusMessage (GstBus *, GstMessage *msg)
{
    switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_ELEMENT:
        if (msg->structure && gst_structure_has_name (msg->structure, "prepare-xwindow-id")) {
            gst_x_overlay_set_window_handle (GST_X_OVERLAY (GST_MESSAGE_SRC (msg)),
                 getWinId());
            gst_message_unref (msg);
            return GST_BUS_DROP;
        }
        break;
    default:
        break;
    }

    return GST_BUS_PASS;
}

static gboolean
bus_call(GstBus * bus, GstMessage * msg, gpointer data)
{
    QDeclarativeVideoEditor *self = (QDeclarativeVideoEditor*) data;
    return self->handleBusMessage(bus, msg);
}


static GstBusSyncReply
bus_sync_call(GstBus * bus, GstMessage * msg, gpointer data)
{
    QDeclarativeVideoEditor *self = (QDeclarativeVideoEditor*) data;
    return self->handleSyncBusMessage(bus, msg);
}

QString getDateTimeString() {
    QDateTime current = QDateTime::currentDateTime();

    return current.toString("yyyyMMdd-hhmmss");
}

QString getFileName() {
    QString timeString = getDateTimeString();

    for(int i = 1; i < 10000; i++){
        QString filename = QString(MOVIES_DIR + timeString + "-%1.mp4").arg(i, 4, 10, QLatin1Char('0'));

        if(!QFile(filename).exists()) {
            return filename;
        }
    }

    return QString();
}

GESTimelinePipeline *QDeclarativeVideoEditor::getPipeline() const
{
    return m_pipeline;
}

void QDeclarativeVideoEditor::updateDuration()
{
    quint64 list_duration = 0;
    QList<VideoEditorItem*>::iterator i;
    for (i = m_items.begin(); i != m_items.end(); ++i) {
        if ((*i)->getDuration() != -1)
            list_duration += (*i)->getDuration();
    }

    qDebug() << "List duration: " << list_duration;
    m_duration = list_duration;
    emit durationChanged();
}

qint64 QDeclarativeVideoEditor::getDuration() const
{
    return m_duration;
}

void QDeclarativeVideoEditor::setDuration(qint64 duration)
{
    qDebug() << "Composition duration: " << m_duration << " to " << duration;
    m_duration = duration;
}

double QDeclarativeVideoEditor::getProgress() const
{
    return m_progress;
}

void QDeclarativeVideoEditor::setProgress(double progress)
{
    m_progress = progress;
    emit progressChanged();
}

qint64 QDeclarativeVideoEditor::getPosition() const
{
    return this->m_position;
}

void QDeclarativeVideoEditor::setPosition(qint64 position)
{
    this->m_position = position;
    emit positionChanged();
}

void QDeclarativeVideoEditor::updatePosition()
{
    double progress = this->getProgress();
    if (progress == -1.0) {
        qDebug() << "Stopping progress polling";
        setProgress(0);
        m_positionTimer.stop();
        return;
    }

    double duration = this->getDuration();
    if(duration == -1) {
        //unknown
        this->setProgress(0);
    } else {
        gint64 cur_pos = GST_CLOCK_TIME_NONE;
        GstFormat format_time = GST_FORMAT_TIME;
        gst_element_query_position (GST_ELEMENT (this->getPipeline()), &format_time, &cur_pos);

        if(duration < 0 || cur_pos < 0) {
            this->setProgress (0);
            this->setPosition(0);
        } else {
            this->setPosition(cur_pos);
            this->setProgress ((double)cur_pos / duration);
        }
    }

    if (this->getProgress() < 0.0) {
        this->setProgress(0.0);
        this->setPosition(0);
        qDebug() << "Stopping progress polling";
        m_positionTimer.stop();
        return;
    }
}

void QDeclarativeVideoEditor::setRenderSettings(int width, int height, int fps_n, int fps_d)
{
    this->m_width = width;
    this->m_height = height;
    this->m_fpsn = fps_n;
    this->m_fpsd = fps_d;
    emit renderResolutionChanged();
}

bool QDeclarativeVideoEditor::render()
{
    //sanity check
    if (m_size < 1) {
        emit error(NO_MEDIA, "No media added to the timeline");
        return false;
    }

    qDebug() << "Render preparations started";

    QString output_uri = getFileName();
    if(output_uri.isEmpty()) {
        emit error(RENDERING_FAILED, "No filename available");
        return false;
    }

    m_filename = output_uri;
    emit filenameChanged();
    output_uri = "file://" + output_uri;

    GstEncodingProfile *profile = createEncodingProfile();
    if (!ges_timeline_pipeline_set_render_settings (m_pipeline, output_uri.toUtf8().data(), profile)) {
        emit error(RENDERING_FAILED, "Failed setting rendering options");
        gst_encoding_profile_unref(profile);
        return false;
    }
    gst_encoding_profile_unref (profile);

    if (!ges_timeline_pipeline_set_mode (m_pipeline, TIMELINE_MODE_RENDER)) {
        emit error(RENDERING_FAILED, "Failed to set rendering mode");
        gst_encoding_profile_unref(profile);
        return false;
    }

    qDebug() << "Rendering to " << output_uri;

    // reset duration and progress
    updateDuration();
    setProgress(0.0);
    qDebug() << "Starting progress polling";

    m_positionTimer.start(PROGRESS_TIMEOUT);

    m_rendering = true;
    if(!gst_element_set_state (GST_ELEMENT (m_pipeline), GST_STATE_PLAYING)) {
        gst_element_set_state (GST_ELEMENT (m_pipeline), GST_STATE_NULL);

        m_rendering = false;
        emit error(RENDERING_FAILED, "Failed to set pipeline to playing state");
        return false;
    }
    emit playingStateChanged();
    return true;
}

void QDeclarativeVideoEditor::cancelRender()
{
    qDebug() << "Cancelling rendering operation";
    gst_element_set_state (GST_ELEMENT (m_pipeline), GST_STATE_PAUSED);
    setProgress(-1.0);
    m_rendering = false;
    ges_timeline_pipeline_set_mode (m_pipeline, TIMELINE_MODE_PREVIEW);
    emit playingStateChanged();
}

uint QDeclarativeVideoEditor::getWinId() const
{
    return m_winId;
}

void QDeclarativeVideoEditor::setWinId(uint winId)
{
    m_winId = winId;
    gst_x_overlay_set_xwindow_id (GST_X_OVERLAY (m_vsink), m_winId);
}

void QDeclarativeVideoEditor::play()
{
    m_positionTimer.start(PROGRESS_TIMEOUT);
    gst_element_set_state (GST_ELEMENT (m_pipeline), GST_STATE_PLAYING);
    emit playingStateChanged();
}


void QDeclarativeVideoEditor::pause()
{
    m_positionTimer.stop();
    gst_element_set_state (GST_ELEMENT (m_pipeline), GST_STATE_PAUSED);

    emit playingStateChanged();
}

void QDeclarativeVideoEditor::seek(qint64 position)
{
    qDebug() << "Seeking to" << position;
    if(!gst_element_seek_simple(GST_ELEMENT (m_pipeline), GST_FORMAT_TIME, (GstSeekFlags)
                            (GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE), position)) {
        qDebug() << "Seek failed";
    }
}

bool QDeclarativeVideoEditor::isRendering() const
{
    return m_rendering;
}

uint QDeclarativeVideoEditor::getRenderWidth() const
{
    return m_width;
}

uint QDeclarativeVideoEditor::getRenderHeight() const
{
    return m_height;
}

uint QDeclarativeVideoEditor::getRenderFpsN() const
{
    return m_fpsn;
}

uint QDeclarativeVideoEditor::getRenderFpsD() const
{
    return m_fpsd;
}

QString QDeclarativeVideoEditor::getFilename() const
{
    return m_filename;
}

bool QDeclarativeVideoEditor::isPlaying() const
{
    GstState state;
    GstState pending;

    gst_element_get_state(GST_ELEMENT(m_pipeline), &state, &pending, 0);

    return state == GST_STATE_PLAYING && pending == GST_STATE_VOID_PENDING && !m_rendering;
}
