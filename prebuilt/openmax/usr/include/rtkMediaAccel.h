#ifndef __RTKMEDIAACCEL_H__
#define __RTKMEDIAACCEL_H__


typedef enum {
    RMA_SUCCESS = 0,
    RMA_ERR_FAILURE = -1,
    RMA_ERR_INVALID_OPERATION = -2,
    RMA_ERR_INVALID_PARAM = -3,
    RMA_ERR_INSUFFICIENT_RESOURCE = -4,
    RMA_ERR_AGAIN = -5,
    RMA_ERR_DECODER_NOT_FOUNT = -6,
    RMA_ERR_NOT_SUPPORT = -7,
} RMA_ERRORTYPE;

typedef enum {
    RMA_ENQUEUE_CONFIG = 0,
    RMA_ENQUEUE_FRAME = 1,
    RMA_ENQUEUE_EOS = 2,
} RMA_ENQUEUETYPE;

typedef enum {
    RMA_CODEC_MPEG4,
    RMA_CODEC_H264,
    RMA_CODEC_HEVC,
    RMA_CODEC_MPEG1,
    RMA_CODEC_MPEG2,
    RMA_CODEC_VP8,
    RMA_CODEC_VP9,
    RMA_CODEC_VC1,
    RMA_CODEC_WMV3,
    RMA_CODEC_DIVX3,
    RMA_CODEC_RV30,
    RMA_CODEC_RV40,
    RMA_CODEC_MJPEG,
    RMA_CODEC_H263,
    RMA_CODEC_AVS,
    RMA_CODEC_FLV,
    RMA_CODEC_NONE,
} RMA_CODEC;

typedef enum {
    RMA_COLOR_FORMAT_YUV420_SEMIPLANAR,
    RMA_COLOR_FORMAT_YUV420_PLANAR,
    RMA_COLOR_FORMAT_NONE,
} RMA_COLOR_FORMAT;

typedef struct RMA_BUFFERINFO
{
    char rtk_buffer_id[4];
    unsigned char* pBuffer;            /**< Pointer to actual block of memory
                                     that is acting as the buffer */
    unsigned int nAllocLen;          /**< size of the buffer allocated, in bytes */
    unsigned int nFilledLen;         /**< number of bytes currently in the
                                     buffer */
    long long nTimeStamp;          /**< Timestamp corresponding to the sample
                                     starting at the first logical sample
                                     boundary in the buffer. Timestamps of
                                     successive samples within the buffer may
                                     be inferred by adding the duration of the
                                     of the preceding buffer to the timestamp
                                     of the preceding buffer.*/
    unsigned int phyBufAddr;
    unsigned int stride, plane_size;
    unsigned int width, height;
    void* bufferHeader;
    void* handler;
    unsigned int bufState;
} RMA_BUFFERINFO;

typedef struct RMA_OUTPUTINFO
{
    unsigned int width, height;
    unsigned int stride, plane_size;
    RMA_COLOR_FORMAT colorFmt;
} RMA_OUTPUTINFO;

typedef struct RMA_PARAMETER
{
    RMA_CODEC codec;
    unsigned int ori_width;
    unsigned int ori_height;
    unsigned int dec_o_width;
    unsigned int dec_o_height;
    unsigned int dec_o_fps;
    unsigned int auto_resize;
    unsigned int turbo_mode;
    unsigned int search_I_frm;
    unsigned int search_I_err_tolerance;
    unsigned int omx_version;
    unsigned int renderFlg;
    unsigned int dec_select;
    void *pUserData;
    void (*setParam) (void *, RMA_COLOR_FORMAT, unsigned int, unsigned int);
} RMA_PARAMETER;

void* RMA_Init(const char * role);
void* RMA_Uninit(void *handler);
RMA_ERRORTYPE RMA_Start(void *handler, RMA_PARAMETER param);
RMA_ERRORTYPE RMA_FreeBuffer(void *handler, void *data);
RMA_ERRORTYPE RMA_EnqueueInputBuffer(void *handler, unsigned char *data, unsigned int size, long long timeStamp, RMA_ENQUEUETYPE type);
RMA_ERRORTYPE RMA_DequeueOutputBuffer(void *handler, unsigned int *outChange, RMA_BUFFERINFO *disp_buffer);
RMA_ERRORTYPE RMA_Flush(void *handler);
RMA_ERRORTYPE RMA_CheckValidBuffer(void *handler, unsigned char *data);
RMA_ERRORTYPE RMA_Memcpy(void *handler, void *dst, void *src, unsigned int size);

#endif
