#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <gst/rtsp-server/rtsp-server.h>

#include "deepstream_common.h"
#include "deepstream_sinks.h"
#include "gstnvdsmeta.h"
#include "pipeline.h"

gint frame_number = 0;
static guint uid = 0;
static GstRTSPServer *server [MAX_SINK_BINS];
static guint server_count = 0;
static GMutex server_cnt_lock;

bool sendKafkamessage(char * msg){
  std::cout<<"Class: "<<msg<<std::endl;
  return true;
}

/* osd_sink_pad_buffer_probe  will extract metadata received on OSD sink pad
 * and update params for drawing rectangle, object information etc. */

static GstPadProbeReturn
osd_sink_pad_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
    GstBuffer *buf = (GstBuffer *) info->data;
    guint num_rects = 0; 
    NvDsObjectMeta *obj_meta = NULL;
    guint vehicle_count = 0;
    guint person_count = 0;
    NvDsMetaList * l_frame = NULL;
    NvDsMetaList * l_obj = NULL;
    NvDsDisplayMeta *display_meta = NULL;

    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);

    for (l_frame = batch_meta->frame_meta_list; l_frame != NULL;
      l_frame = l_frame->next) {
        NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) (l_frame->data);
        int offset = 0;
        for (l_obj = frame_meta->obj_meta_list; l_obj != NULL;
                l_obj = l_obj->next) {
            obj_meta = (NvDsObjectMeta *) (l_obj->data);
            if (obj_meta->class_id == PGIE_CLASS_ID_WEAPON) {
              sendKafkamessage("weapon");
            }
        }
    }
    return GST_PAD_PROBE_OK;
}

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
    GMainLoop *loop = (GMainLoop *) data;
    switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_EOS:
            g_print ("End of stream\n");
            g_main_loop_quit (loop);
            break;
        case GST_MESSAGE_ERROR:{
            gchar *debug;
            GError *error;
            gst_message_parse_error (msg, &error, &debug);
            g_printerr ("ERROR from element %s: %s\n",
                GST_OBJECT_NAME (msg->src), error->message);
            if (debug)
                g_printerr ("Error details: %s\n", debug);
            g_free (debug);
            g_error_free (error);
            g_main_loop_quit (loop);
            break;
        }
        default:
            break;
    }
  return TRUE;
}

static void
cb_newpad (GstElement * decodebin, GstPad * decoder_src_pad, gpointer data)
{
    g_print ("In cb_newpad\n");
    GstCaps *caps = gst_pad_get_current_caps (decoder_src_pad);
    const GstStructure *str = gst_caps_get_structure (caps, 0);
    const gchar *name = gst_structure_get_name (str);
    GstElement *source_bin = (GstElement *) data;
    GstCapsFeatures *features = gst_caps_get_features (caps, 0);

    /* Need to check if the pad created by the decodebin is for video and not
    * audio. */
    if (!strncmp (name, "video", 5)) {
        /* Link the decodebin pad only if decodebin has picked nvidia
        * decoder plugin nvdec_*. We do this by checking if the pad caps contain
        * NVMM memory features. */
        if (gst_caps_features_contains (features, GST_CAPS_FEATURES_NVMM)) {
        /* Get the source bin ghost pad */
        GstPad *bin_ghost_pad = gst_element_get_static_pad (source_bin, "src");
        if (!gst_ghost_pad_set_target (GST_GHOST_PAD (bin_ghost_pad),
                decoder_src_pad)) {
            g_printerr ("Failed to link decoder src pad to source bin ghost pad\n");
        }
        gst_object_unref (bin_ghost_pad);
        } else {
            g_printerr ("Error: Decodebin did not pick nvidia decoder plugin.\n");
        }
    }
}

static void
decodebin_child_added (GstChildProxy * child_proxy, GObject * object,
    gchar * name, gpointer user_data)
{
    g_print ("Decodebin child added: %s\n", name);
    if (g_strrstr (name, "decodebin") == name) {
        g_signal_connect (G_OBJECT (object), "child-added",
            G_CALLBACK (decodebin_child_added), user_data);
    }
}

static GstElement *
create_source_bin (guint index, gchar * uri)
{
    GstElement *bin = NULL, *uri_decode_bin = NULL;
    gchar bin_name[16] = { };

    g_snprintf (bin_name, 15, "source-bin-%02d", index);
    /* Create a source GstBin to abstract this bin's content from the rest of the
    * pipeline */
    bin = gst_bin_new (bin_name);

    /* Source element for reading from the uri.
    * We will use decodebin and let it figure out the container format of the
    * stream and the codec and plug the appropriate demux and decode plugins. */
    uri_decode_bin = gst_element_factory_make ("uridecodebin", "uri-decode-bin");

    if (!bin || !uri_decode_bin) {
        g_printerr ("One element in source bin could not be created.\n");
        return NULL;
    }

    /* We set the input uri to the source element */
    g_object_set (G_OBJECT (uri_decode_bin), "uri", uri, NULL);

    /* Connect to the "pad-added" signal of the decodebin which generates a
    * callback once a new pad for raw data has beed created by the decodebin */
    g_signal_connect (G_OBJECT (uri_decode_bin), "pad-added",
        G_CALLBACK (cb_newpad), bin);
    g_signal_connect (G_OBJECT (uri_decode_bin), "child-added",
        G_CALLBACK (decodebin_child_added), bin);

    gst_bin_add (GST_BIN (bin), uri_decode_bin);

    /* We need to create a ghost pad for the source bin which will act as a proxy
    * for the video decoder src pad. The ghost pad will not have a target right
    * now. Once the decode bin creates the video decoder and generates the
    * cb_newpad callback, we will set the ghost pad target to the video decoder
    * src pad. */
    if (!gst_element_add_pad (bin, gst_ghost_pad_new_no_target ("src",
                GST_PAD_SRC))) {
        g_printerr ("Failed to add ghost pad in source bin\n");
        return NULL;
    }

    return bin;
}

static gboolean
start_rtsp_streaming (guint rtsp_port_num, guint updsink_port_num)
{
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  char udpsrc_pipeline[512];

  char port_num_Str[64] = { 0 };
  char *encoder_name;

  encoder_name = "H264";

  sprintf (udpsrc_pipeline,
      "( udpsrc name=pay0 port=%d caps=\"application/x-rtp, media=video, "
      "clock-rate=90000, encoding-name=%s, payload=96 \" )",
      updsink_port_num, encoder_name);

  sprintf (port_num_Str, "%d", rtsp_port_num);

  g_mutex_lock (&server_cnt_lock);

  server [server_count] = gst_rtsp_server_new ();
  g_object_set (server [server_count], "service", port_num_Str, NULL);

  mounts = gst_rtsp_server_get_mount_points (server [server_count]);

  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory, udpsrc_pipeline);

  gst_rtsp_mount_points_add_factory (mounts, "/ds-test", factory);

  g_object_unref (mounts);

  gst_rtsp_server_attach (server [server_count], NULL);

  server_count++;

  g_mutex_unlock (&server_cnt_lock);

  g_print
      ("\n *** DeepStream: Launched RTSP Streaming at rtsp://localhost:%d/ds-test ***\n\n",
      rtsp_port_num);

  return TRUE;
}

static GstElement *
create_udpsink_bin (unsigned int i)
{
  GstCaps *caps = NULL;
  gboolean ret = FALSE;
  gchar elem_name[50];
  gchar encode_name[50];
  gchar rtppay_name[50];
  GstElement *bin = NULL;
  GstElement *queue = NULL;
  GstElement *transform = NULL;
  GstElement *cap_filter = NULL;
  GstElement *enc_caps_filter = NULL;
  GstElement *encoder = NULL;
  GstElement *codecparse = NULL;
  GstElement *sink = NULL;
  GstElement *rtppay = NULL;

  //guint rtsp_port_num = g_rtsp_port_num++;
  uid++;

  g_snprintf (elem_name, sizeof (elem_name), "sink_sub_bin%d", i);
  bin = gst_bin_new (elem_name);
  if (!bin) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", elem_name);
    goto done;
  }

  g_snprintf (elem_name, sizeof (elem_name), "sink_sub_bin_queue%d", i);
  queue = gst_element_factory_make (NVDS_ELEM_QUEUE, elem_name);
  if (!queue) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", elem_name);
    goto done;
  }

  g_snprintf (elem_name, sizeof (elem_name), "sink_sub_bin_transform%d", i);
  transform = gst_element_factory_make (NVDS_ELEM_VIDEO_CONV, elem_name);
  if (!transform) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", elem_name);
    goto done;
  }

  g_snprintf (elem_name, sizeof (elem_name), "sink_sub_bin_cap_filter%d", i);
  cap_filter = gst_element_factory_make (NVDS_ELEM_CAPS_FILTER, elem_name);
  if (!cap_filter) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", elem_name);
    goto done;
  }

  caps = gst_caps_from_string ("video/x-raw(memory:NVMM), format=I420");

  g_object_set (G_OBJECT (cap_filter), "caps", caps, NULL);

  g_snprintf (encode_name, sizeof (encode_name), "sink_sub_bin_encoder%d", i);
  g_snprintf (rtppay_name, sizeof (rtppay_name), "sink_sub_bin_rtppay%d", i);

  codecparse = gst_element_factory_make ("h264parse", "h264-parser");
  rtppay = gst_element_factory_make ("rtph264pay", rtppay_name);
  encoder = gst_element_factory_make (NVDS_ELEM_ENC_H264_HW, encode_name);

  if (!encoder) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", encode_name);
    goto done;
  }

  if (!rtppay) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", rtppay_name);
    goto done;
  }

  g_object_set (G_OBJECT (encoder), "bitrate", 4096000, NULL);
//   g_object_set (G_OBJECT (encoder), "profile", config->profile, NULL);
  g_object_set (G_OBJECT (encoder), "iframeinterval", 10, NULL);

//   g_object_set (G_OBJECT (transform), "gpu-id", 0, NULL);

  g_snprintf (elem_name, sizeof (elem_name), "sink_sub_bin_udpsink%d", i);
  sink = gst_element_factory_make ("udpsink", elem_name);
  if (!sink) {
    NVGSTDS_ERR_MSG_V ("Failed to create '%s'", elem_name);
    goto done;
  }

  g_object_set (G_OBJECT (sink), "host", "224.224.255.255", "port",
      5000+i, "async", FALSE, "sync", 0, NULL);

  gst_bin_add_many (GST_BIN (bin),
      queue, cap_filter, transform,
      encoder, codecparse, rtppay, sink, NULL);

  NVGSTDS_LINK_ELEMENT (queue, transform);
  NVGSTDS_LINK_ELEMENT (transform, cap_filter);
  NVGSTDS_LINK_ELEMENT (cap_filter, encoder);
  NVGSTDS_LINK_ELEMENT (encoder, codecparse);
  NVGSTDS_LINK_ELEMENT (codecparse, rtppay);
  NVGSTDS_LINK_ELEMENT (rtppay, sink);

  NVGSTDS_BIN_ADD_GHOST_PAD (bin, queue, "sink");

  ret = TRUE;

  ret = start_rtsp_streaming (8554+i, 5000+i);
  if (ret != TRUE) {
    g_print ("%s: start_rtsp_straming function failed\n", __func__);
  }

done:
  if (caps) {
    gst_caps_unref (caps);
  }
  if (!ret) {
    NVGSTDS_ERR_MSG_V ("%s failed", __func__);
  }
  return bin;
}

int
main (int argc, char *argv[])
{
    /* Standard GStreamer initialization */
    gst_init (&argc, &argv);

    Pipeline detector;
    detector.loop = g_main_loop_new (NULL, FALSE);

    detector.createElements();
    detector.Verify();
    detector.Configure();

    /* we add a message handler */
    detector.bus = gst_pipeline_get_bus (GST_PIPELINE (detector.pipeline));
    detector.bus_watch_id = gst_bus_add_watch (detector.bus, bus_call, detector.loop);
    gst_object_unref (detector.bus);

    detector.ConstructPipeline();

    /* Lets add probe to get informed of the meta data generated, we add probe to
        * the sink pad of the osd element, since by that time, the buffer would have
        * had got all the metadata. */

    for (int i = 0; i < 1; i++) {
        string uri = "";
        cout << "Adding: " << argv[1] << endl;

        GstPad *sinkpad, *srcpad;
        gchar pad_name[16] = { };

        GstElement *source_bin = create_source_bin (i, argv[1]);

        if (!source_bin) {
            g_printerr ("Failed to create source bin. Exiting.\n");
            return -1;
        }

        gst_bin_add (GST_BIN (detector.pipeline), source_bin);

        g_snprintf (pad_name, 15, "sink_%u", i);
        sinkpad = gst_element_get_request_pad (detector.streammux, pad_name);
        if (!sinkpad) {
            g_printerr ("Streammux request sink pad failed. Exiting.\n");
            return -1;
        }

        srcpad = gst_element_get_static_pad (source_bin, "src");
        if (!srcpad) {
            g_printerr ("Failed to get src pad of source bin. Exiting.\n");
            return -1;
        }

        if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK) {
            g_printerr ("Failed to link source bin to stream muxer. Exiting.\n");
            return -1;
        }

        gst_object_unref (srcpad);
        gst_object_unref (sinkpad);

        GstElement *rtspbin;
        rtspbin = create_udpsink_bin(i);
        gst_bin_add (GST_BIN (detector.pipeline), rtspbin);
        gst_element_link(detector.nvosd, rtspbin);
    }
  GstPad *osd_sink_pad = NULL;
  osd_sink_pad = gst_element_get_static_pad (detector.nvosd, "sink");
  if (!osd_sink_pad)
    g_print ("Unable to get sink pad\n");
  else
    gst_pad_add_probe (osd_sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
        osd_sink_pad_buffer_probe, NULL, NULL);
  gst_object_unref (osd_sink_pad);

    // Set the pipeline to "playing" state
    gst_element_set_state(detector.pipeline, GST_STATE_PLAYING);
    g_main_loop_run(detector.loop);
    /* Out of the main loop, clean up nicely */
    g_print ("Returned, stopping playback\n");
    gst_element_set_state (detector.pipeline, GST_STATE_NULL);
    g_print ("Deleting pipeline\n");
    gst_object_unref (GST_OBJECT (detector.pipeline));
    g_source_remove (detector.bus_watch_id);
    g_main_loop_unref (detector.loop);
    return 0;
}
