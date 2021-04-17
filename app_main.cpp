#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <fstream>
#include <sstream>

#include "gstnvdsmeta.h"
#include "pipeline.h"

gint frame_number = 0;

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

    }

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
