#include "pipeline.h"
#include <unistd.h>

Pipeline::Pipeline()
{
}

void Pipeline::createElements()
{
    /* Create gstreamer elements */
    /* Create Pipeline element that will form a connection of other elements */
    pipeline = gst_pipeline_new ("detector-pipeline");

    /* Create nvstreammux instance to form batches from one or more sources. */
    streammux = gst_element_factory_make ("nvstreammux", "stream-muxer");

    /* Use nvinfer to infer on batched frame. */
    pgie = gst_element_factory_make ("nvinfer", "primary-nvinference-engine");

    /* Use convertor to convert from NV12 to RGBA as required by nvosd */
    nvvidconv = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter");

    /* Create OSD to draw on the converted RGBA buffer */
    nvosd = gst_element_factory_make ("nvdsosd", "nv-onscreendisplay");

    /* Finally render the osd output */
    sink = gst_element_factory_make ("nveglglessink", "nvvideo-renderer");
}

void Pipeline::Verify()
{
    if (!pipeline || !streammux) {
        g_printerr ("Initial elements could not be created. Exiting.\n");
        exit(-1);
    }

    if (!pgie || !nvvidconv || !nvosd || !sink) {
        g_printerr ("Pipeline elements could not be created. Exiting.\n");
        exit(-1);
    }
}

void Pipeline::Configure()
{
    g_object_set (G_OBJECT (streammux), "batch-size", 1, NULL);

    g_object_set (G_OBJECT (streammux), "width", MUXER_OUTPUT_WIDTH, "height",
        MUXER_OUTPUT_HEIGHT,
        "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC, NULL);

    /* Configure the nvinfer element using the nvinfer config file. */
    g_object_set (G_OBJECT (pgie),
        "config-file-path", PGIE_CONFIG_FILE, NULL);

    // g_object_set (G_OBJECT (sink), "qos", 0, "sync", 1, NULL);
}

void Pipeline::ConstructPipeline()
{
  gst_bin_add (GST_BIN (pipeline), streammux);

    /* Set up the pipeline */
    /* we add all elements into the pipeline */
    gst_bin_add_many (GST_BIN (pipeline), pgie,
        nvvidconv, nvosd, NULL);
    /* we link the elements together
    * nvstreammux -> nvinfer -> nvtiler -> nvvidconv -> nvosd -> video-renderer */
    if (!gst_element_link_many (streammux, pgie,
            nvvidconv, nvosd, NULL)) {
        g_printerr ("Elements could not be linked. Exiting.\n");
        exit(-1);
    }
}

Pipeline::~Pipeline()
{
}
