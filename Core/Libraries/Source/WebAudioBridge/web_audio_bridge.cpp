#include "mss/mss.h"
#include <Utility/gx_trace.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <emscripten.h>

extern "C" char* WebAudioBridge_ReadEntireFile(const char* filename, uint32_t* out_size);

// IMA ADPCM tables
static const int indexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const int stepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

struct WavHeader {
    char riff[4];          // "RIFF"
    uint32_t riff_size;    // size of file - 8
    char wave[4];          // "WAVE"
    char fmt[4];           // "fmt "
    uint32_t fmt_size;     // 16
    uint16_t format_tag;   // 1 (PCM)
    uint16_t channels;     // channels
    uint32_t sample_rate;  // rate
    uint32_t byte_rate;    // rate * channels * 2
    uint16_t block_align;  // channels * 2
    uint16_t bits;         // 16
    char data[4];          // "data"
    uint32_t data_size;    // PCM data size
};

struct WavHeaderInfo {
    int format;
    int channels;
    int sample_rate;
    int bits_per_sample;
    const uint8_t* data_ptr;
    uint32_t data_len;
    uint32_t block_align;
};

static bool parse_wav(const void* file_image, WavHeaderInfo& info) {
    if (!file_image) return false;
    const uint8_t* buf = (const uint8_t*)file_image;
    if (memcmp(buf, "RIFF", 4) != 0) return false;
    uint32_t riff_size = *(const uint32_t*)(buf + 4);
    if (memcmp(buf + 8, "WAVE", 4) != 0) return false;

    const uint8_t* p = buf + 12;
    const uint8_t* end = buf + 8 + riff_size;
    info.data_ptr = nullptr;
    info.data_len = 0;
    info.block_align = 0;

    while (p + 8 <= end) {
        char chunk_id[4];
        memcpy(chunk_id, p, 4);
        uint32_t chunk_size = *(const uint32_t*)(p + 4);
        p += 8;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size >= 16) {
                info.format = *(const uint16_t*)p;
                info.channels = *(const uint16_t*)(p + 2);
                info.sample_rate = *(const uint32_t*)(p + 4);
                info.bits_per_sample = *(const uint16_t*)(p + 14);
                if (chunk_size >= 32) {
                    info.block_align = *(const uint16_t*)(p + 32);
                } else if (chunk_size >= 20) {
                    info.block_align = *(const uint16_t*)(p + 12);
                }
            }
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            info.data_ptr = p;
            info.data_len = chunk_size;
        }
        p += chunk_size;
        if (chunk_size & 1) p++;
    }
    return info.data_ptr != nullptr;
}

static void decode_block_mono(const uint8_t* src, uint32_t block_size, int16_t* dst) {
    int16_t pred = *(const int16_t*)src;
    int8_t idx = src[2];
    src += 4;

    *dst++ = pred;

    const uint8_t* block_end = src - 4 + block_size;
    while (src < block_end) {
        uint8_t val = *src++;
        // Low nibble
        uint8_t nibble = val & 0x0F;
        int step = stepTable[idx];
        int diff = step >> 3;
        if (nibble & 4) diff += step;
        if (nibble & 2) diff += step >> 1;
        if (nibble & 1) diff += step >> 2;
        if (nibble & 8) pred -= diff; else pred += diff;
        if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
        idx += indexTable[nibble];
        if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
        *dst++ = pred;

        // High nibble
        nibble = (val >> 4) & 0x0F;
        step = stepTable[idx];
        diff = step >> 3;
        if (nibble & 4) diff += step;
        if (nibble & 2) diff += step >> 1;
        if (nibble & 1) diff += step >> 2;
        if (nibble & 8) pred -= diff; else pred += diff;
        if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
        idx += indexTable[nibble];
        if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
        *dst++ = pred;
    }
}

static void decode_block_stereo(const uint8_t* src, uint32_t block_size, int16_t* dst_interleaved) {
    int16_t pred_l = *(const int16_t*)src;
    int8_t idx_l = src[2];
    int16_t pred_r = *(const int16_t*)(src + 4);
    int8_t idx_r = src[6];
    src += 8;

    int16_t samples_l[4096];
    int16_t samples_r[4096];
    int out_l = 0;
    int out_r = 0;

    samples_l[out_l++] = pred_l;
    samples_r[out_r++] = pred_r;

    const uint8_t* block_end = src - 8 + block_size;
    while (src < block_end) {
        for (int i = 0; i < 8 && src < block_end; i++) {
            uint8_t val = *src++;
            uint8_t nibble = val & 0x0F;
            int step = stepTable[idx_l];
            int diff = step >> 3;
            if (nibble & 4) diff += step;
            if (nibble & 2) diff += step >> 1;
            if (nibble & 1) diff += step >> 2;
            if (nibble & 8) pred_l -= diff; else pred_l += diff;
            if (pred_l > 32767) pred_l = 32767; else if (pred_l < -32768) pred_l = -32768;
            idx_l += indexTable[nibble];
            if (idx_l < 0) idx_l = 0; else if (idx_l > 88) idx_l = 88;
            samples_l[out_l++] = pred_l;

            nibble = (val >> 4) & 0x0F;
            step = stepTable[idx_l];
            diff = step >> 3;
            if (nibble & 4) diff += step;
            if (nibble & 2) diff += step >> 1;
            if (nibble & 1) diff += step >> 2;
            if (nibble & 8) pred_l -= diff; else pred_l += diff;
            if (pred_l > 32767) pred_l = 32767; else if (pred_l < -32768) pred_l = -32768;
            idx_l += indexTable[nibble];
            if (idx_l < 0) idx_l = 0; else if (idx_l > 88) idx_l = 88;
            samples_l[out_l++] = pred_l;
        }
        for (int i = 0; i < 8 && src < block_end; i++) {
            uint8_t val = *src++;
            uint8_t nibble = val & 0x0F;
            int step = stepTable[idx_r];
            int diff = step >> 3;
            if (nibble & 4) diff += step;
            if (nibble & 2) diff += step >> 1;
            if (nibble & 1) diff += step >> 2;
            if (nibble & 8) pred_r -= diff; else pred_r += diff;
            if (pred_r > 32767) pred_r = 32767; else if (pred_r < -32768) pred_r = -32768;
            idx_r += indexTable[nibble];
            if (idx_r < 0) idx_r = 0; else if (idx_r > 88) idx_r = 88;
            samples_r[out_r++] = pred_r;

            nibble = (val >> 4) & 0x0F;
            step = stepTable[idx_r];
            diff = step >> 3;
            if (nibble & 4) diff += step;
            if (nibble & 2) diff += step >> 1;
            if (nibble & 1) diff += step >> 2;
            if (nibble & 8) pred_r -= diff; else pred_r += diff;
            if (pred_r > 32767) pred_r = 32767; else if (pred_r < -32768) pred_r = -32768;
            idx_r += indexTable[nibble];
            if (idx_r < 0) idx_r = 0; else if (idx_r > 88) idx_r = 88;
            samples_r[out_r++] = pred_r;
        }
    }

    for (int i = 0; i < out_l && i < out_r; i++) {
        *dst_interleaved++ = samples_l[i];
        *dst_interleaved++ = samples_r[i];
    }
}

EM_JS(void, AILBridge_init, (), {
    if (!window.AILBridge) {
        window.AILBridge = {
            audioContext: null,
            samples: {},
            
            init() {
                window.gxAudioVerbose && console.log("[AILBridge] init() called");
                if (!this.audioContext) {
                    this.audioContext = new (window.AudioContext || window.webkitAudioContext)();
                    window.gxAudioVerbose && console.log("[AILBridge] AudioContext created, state:", this.audioContext.state);
                    if (window['Module']) {
                        if (!window['Module']['SDL2']) window['Module']['SDL2'] = {};
                        window['Module']['SDL2']['audioContext'] = this.audioContext;
                    }
                } else {
                    window.gxAudioVerbose && console.log("[AILBridge] AudioContext already exists, state:", this.audioContext.state);
                }
            },
            
            getSample(id) {
                if (!this.samples[id]) {
                    this.samples[id] = {
                        buffer: null,
                        source: null,
                        gainNode: null,
                        pannerNode: null,
                        state: 'empty',
                        playWhenReady: false,
                        loop: false,
                        volume: 1.0,
                        pan: 0.5,
                        min_dist: 10.0,
                        max_dist: 1000.0,
                        x: 0.0, y: 0.0, z: 0.0,
                        startTime: 0.0,
                        
                        // For streaming:
                        audio: null,
                        mediaSourceNode: null,
                        objectURL: null,
                        duration: 0.0
                    };
                }
                return this.samples[id];
            },
            
            setSampleFile(id, file_ptr, is_3d) {
                window.gxAudioVerbose && console.log("[AILBridge] setSampleFile() called: id =", id, "file_ptr =", file_ptr, "is_3d =", is_3d);
                let s = this.getSample(id);
                s.state = 'decoding';
                s.playWhenReady = false;
                
                if (!file_ptr) {
                    console.warn("[AILBridge] setSampleFile: file_ptr is null/0 for id =", id);
                    return;
                }
                let riffSize = HEAPU32[(file_ptr + 4) >> 2];
                let totalSize = riffSize + 8;
                window.gxAudioVerbose && console.log("[AILBridge] setSampleFile: riffSize =", riffSize, "totalSize =", totalSize);
                
                // WebPort: Detect if this is a dummy WAV header wrapping an MP3/Ogg payload.
                // If the first 4 bytes are "RIFF" and bytes 8..11 are "WAVE", check if the payload
                // starting at offset 44 has compressed audio magic numbers (MP3 sync frames or OggS).
                let isCompressed = false;
                if (totalSize > 44) {
                    let byte44 = HEAPU8[file_ptr + 44];
                    let byte45 = HEAPU8[file_ptr + 45];
                    let byte46 = HEAPU8[file_ptr + 46];
                    let isMP3 = (byte44 === 0x49 && byte45 === 0x44 && byte46 === 0x33) || // ID3
                                (byte44 === 0xFF && (byte45 & 0xE0) === 0xE0);             // MP3 Sync
                    let isOgg = (byte44 === 0x4F && byte45 === 0x67 && byte46 === 0x67 && HEAPU8[file_ptr + 47] === 0x53); // OggS
                    isCompressed = isMP3 || isOgg;
                }

                let audioBuffer;
                if (isCompressed) {
                    window.gxAudioVerbose && console.log("[AILBridge] Detected wrapped compressed audio. Peeling 44-byte WAV header.");
                    let compressedBytes = new Uint8Array(HEAPU8.buffer, file_ptr + 44, totalSize - 44).slice();
                    audioBuffer = compressedBytes.buffer;
                } else {
                    let bytes = new Uint8Array(HEAPU8.buffer, file_ptr, totalSize).slice();
                    audioBuffer = bytes.buffer;
                }
                
                this.audioContext.decodeAudioData(audioBuffer, (buffer) => {
                    window.gxAudioVerbose && console.log("[AILBridge] decodeAudioData success for sample id =", id, "duration =", buffer.duration);
                    s.buffer = buffer;
                    s.state = 'ready';
                    if (s.playWhenReady) {
                        window.gxAudioVerbose && console.log("[AILBridge] playing deferred sample id =", id);
                        this.play(id, is_3d, s.loop);
                    }
                }, (err) => {
                    console.error("[AILBridge] decodeAudioData failed for sample id =", id, ":", err);
                    s.state = 'error';
                });
            },
            
            setStreamFile(id, file_ptr, size) {
                window.gxAudioVerbose && console.log("[AILBridge] setStreamFile() called: id =", id, "file_ptr =", file_ptr, "size =", size);
                let s = this.getSample(id + 1000);
                s.state = 'decoding';
                s.playWhenReady = false;
                
                // Clean up previous stream if any
                if (s.audio) {
                    try { s.audio.pause(); } catch(e) {}
                    s.audio = null;
                }
                if (s.mediaSourceNode) {
                    try { s.mediaSourceNode.disconnect(); } catch(e) {}
                    s.mediaSourceNode = null;
                }
                if (s.objectURL) {
                    try { URL.revokeObjectURL(s.objectURL); } catch(e) {}
                    s.objectURL = null;
                }
                
                let bytes = new Uint8Array(HEAPU8.buffer, file_ptr, size).slice();
                let blob = new Blob([bytes], {type: 'audio/mpeg'});
                let url = URL.createObjectURL(blob);
                
                s.objectURL = url;
                
                let audio = new Audio(url);
                audio.preload = 'auto';
                s.audio = audio;
                
                audio.addEventListener('loadedmetadata', () => {
                    s.duration = audio.duration;
                    window.gxAudioVerbose && console.log("[AILBridge] Stream id =", id, "metadata loaded: duration =", s.duration);
                });
                
                try {
                    s.mediaSourceNode = this.audioContext.createMediaElementSource(audio);
                } catch(e) {
                    console.error("[AILBridge] createMediaElementSource failed:", e);
                }
                
                s.state = 'ready';
                if (s.playWhenReady) {
                    window.gxAudioVerbose && console.log("[AILBridge] playing deferred stream id =", id);
                    this.play(id + 1000, 0, s.loop);
                }
            },
            
            play(id, is_3d, loop) {
                window.gxAudioVerbose && console.log("[AILBridge] play() called: id =", id, "is_3d =", is_3d, "loop =", loop);
                let s = this.getSample(id);
                s.loop = loop;
                if (s.state === 'decoding') {
                    window.gxAudioVerbose && console.log("[AILBridge] sample/stream id =", id, "is still decoding. Will play when ready.");
                    s.playWhenReady = true;
                    return;
                }
                if (s.state !== 'ready') {
                    console.warn("[AILBridge] sample/stream id =", id, "is not ready. State:", s.state);
                    return;
                }
                
                let ctx = this.audioContext;
                if (ctx.state === 'suspended') {
                    window.gxAudioVerbose && console.log("[AILBridge] AudioContext is suspended, attempting to resume...");
                    ctx.resume().then(() => {
                        window.gxAudioVerbose && console.log("[AILBridge] AudioContext resumed successfully.");
                    });
                }
                
                let isStream = (id >= 1000);
                
                if (isStream) {
                    if (!s.audio || !s.mediaSourceNode) {
                        console.warn("[AILBridge] stream id =", id, "audio or mediaSourceNode is missing!");
                        return;
                    }
                    window.gxAudioVerbose && console.log("[AILBridge] resetting and playing stream for id =", id);
                    try {
                        s.audio.pause();
                        s.audio.currentTime = 0;
                    } catch(e) {}
                    
                    s.audio.loop = loop;
                    
                    try { s.mediaSourceNode.disconnect(); } catch(e) {}
                    
                    let lastNode = s.mediaSourceNode;
                    let gainNode = ctx.createGain();
                    gainNode.gain.value = s.volume;
                    
                    if (is_3d) {
                        let panner = ctx.createPanner();
                        panner.panningModel = 'HRTF';
                        panner.distanceModel = 'inverse';
                        panner.refDistance = s.min_dist;
                        panner.maxDistance = s.max_dist;
                        if (panner.positionX) {
                            panner.positionX.value = s.x;
                            panner.positionY.value = s.y;
                            panner.positionZ.value = s.z;
                        } else {
                            panner.setPosition(s.x, s.y, s.z);
                        }
                        
                        lastNode.connect(panner);
                        lastNode = panner;
                        s.pannerNode = panner;
                        window.gxAudioVerbose && console.log("[AILBridge] Configured 3D panner for stream id =", id, "pos =", [s.x, s.y, s.z]);
                    } else {
                        let panner = ctx.createStereoPanner();
                        panner.pan.value = (s.pan - 0.5) * 2.0;
                        
                        lastNode.connect(panner);
                        lastNode = panner;
                        s.pannerNode = panner;
                        window.gxAudioVerbose && console.log("[AILBridge] Configured Stereo panner for stream id =", id, "pan =", s.pan);
                    }
                    
                    lastNode.connect(gainNode);
                    gainNode.connect(ctx.destination);
                    
                    s.gainNode = gainNode;
                    s.startTime = ctx.currentTime;
                    
                    s.audio.onended = () => {
                        window.gxAudioVerbose && console.log("[AILBridge] stream.onended fired for id =", id);
                        if (s.audio) {
                            Module['_AIL_trigger_sample_eos'](id - 1000, 2);
                        }
                    };
                    
                    s.audio.play().catch(e => {
                        console.error("[AILBridge] Failed to play stream id =", id, e);
                    });
                    
                } else {
                    if (!s.buffer) {
                        console.warn("[AILBridge] sample id =", id, "buffer is missing!");
                        return;
                    }
                    if (s.source) {
                        window.gxAudioVerbose && console.log("[AILBridge] stopping active source for id =", id);
                        try { s.source.stop(); } catch(e) {}
                        s.source = null;
                    }
                    
                    let source = ctx.createBufferSource();
                    source.buffer = s.buffer;
                    source.loop = loop;
                    
                    let gainNode = ctx.createGain();
                    gainNode.gain.value = s.volume;
                    
                    let lastNode = source;
                    
                    if (is_3d) {
                        let panner = ctx.createPanner();
                        panner.panningModel = 'HRTF';
                        panner.distanceModel = 'inverse';
                        panner.refDistance = s.min_dist;
                        panner.maxDistance = s.max_dist;
                        if (panner.positionX) {
                            panner.positionX.value = s.x;
                            panner.positionY.value = s.y;
                            panner.positionZ.value = s.z;
                        } else {
                            panner.setPosition(s.x, s.y, s.z);
                        }
                        
                        lastNode.connect(panner);
                        lastNode = panner;
                        s.pannerNode = panner;
                        window.gxAudioVerbose && console.log("[AILBridge] Configured 3D panner for sample id =", id, "pos =", [s.x, s.y, s.z]);
                    } else {
                        let panner = ctx.createStereoPanner();
                        panner.pan.value = (s.pan - 0.5) * 2.0;
                        
                        lastNode.connect(panner);
                        lastNode = panner;
                        s.pannerNode = panner;
                        window.gxAudioVerbose && console.log("[AILBridge] Configured Stereo panner for sample id =", id, "pan =", s.pan);
                    }
                    
                    lastNode.connect(gainNode);
                    gainNode.connect(ctx.destination);
                    
                    s.source = source;
                    s.gainNode = gainNode;
                    s.startTime = ctx.currentTime;
                    
                    source.onended = () => {
                        window.gxAudioVerbose && console.log("[AILBridge] source.onended fired for id =", id);
                        if (s.source === source) {
                            s.source = null;
                            Module['_AIL_trigger_sample_eos'](id, is_3d ? 1 : 0);
                        }
                    };
                    
                    window.gxAudioVerbose && console.log("[AILBridge] starting source playback for id =", id);
                    source.start(0);
                }
            },
            
            getStreamPos(id, out_total, out_curr) {
                let s = this.samples[id + 1000];
                if (s) {
                    let total = s.duration ? s.duration : (s.buffer ? s.buffer.duration : 0);
                    if (out_total) {
                        HEAP32[out_total >> 2] = Math.round(total * 1000);
                    }
                    if (out_curr) {
                        let curr = 0;
                        if (s.audio) {
                            curr = s.audio.currentTime * 1000;
                        } else if (s.startTime && s.source && s.buffer) {
                            curr = (this.audioContext.currentTime - s.startTime) * 1000;
                            if (s.loop) {
                                curr = curr % (s.buffer.duration * 1000);
                            } else {
                                curr = Math.min(curr, s.buffer.duration * 1000);
                            }
                        }
                        HEAP32[out_curr >> 2] = Math.round(curr);
                    }
                } else {
                    if (out_total) HEAP32[out_total >> 2] = 0;
                    if (out_curr) HEAP32[out_curr >> 2] = 0;
                }
            }
        };
    }
});

#define MAX_SAMPLES 128
#define MAX_STREAMS 32
#define LISTENER_HANDLE ((H3DPOBJECT)9999)

struct SampleState {
    int id;
    bool allocated;
    const void* file_image;
    AIL_sample_callback eos_callback;
    float volume;
    float pan;
    int loop_count;
    int playback_rate;
    bool is_playing;
};

struct Sample3DState {
    int id;
    bool allocated;
    const void* file_image;
    AIL_3dsample_callback eos_callback;
    float volume;
    float max_dist;
    float min_dist;
    float x, y, z;
    int loop_count;
    int playback_rate;
    bool is_playing;
};

struct StreamState {
    int id;
    bool allocated;
    char filename[256];
    AIL_stream_callback callback;
    float volume;
    float pan;
    int loop_count;
    bool is_playing;
};

static SampleState g_samples[MAX_SAMPLES];
static Sample3DState g_samples3D[MAX_SAMPLES];
static StreamState g_streams[MAX_STREAMS];

extern "C" EMSCRIPTEN_KEEPALIVE void AIL_trigger_sample_eos(int id, int type) {
    if (type == 0) {
        if (id >= 1 && id <= MAX_SAMPLES) {
            SampleState* s = &g_samples[id - 1];
            if (s->allocated) {
                s->is_playing = false;
                if (s->eos_callback) {
                    s->eos_callback((HSAMPLE)s);
                }
            }
        }
    } else if (type == 1) {
        if (id >= 1 && id <= MAX_SAMPLES) {
            Sample3DState* s = &g_samples3D[id - 1];
            if (s->allocated) {
                s->is_playing = false;
                if (s->eos_callback) {
                    s->eos_callback((H3DSAMPLE)s);
                }
            }
        }
    } else if (type == 2) {
        if (id >= 1 && id <= MAX_STREAMS) {
            StreamState* s = &g_streams[id - 1];
            if (s->allocated) {
                s->is_playing = false;
                if (s->callback) {
                    s->callback((HSTREAM)s);
                }
            }
        }
    }
}

extern "C" {

float __stdcall AIL_3D_sample_volume(H3DSAMPLE sample) {
    Sample3DState* s = (Sample3DState*)sample;
    return s->volume;
}

void __stdcall AIL_set_3D_sample_volume(H3DSAMPLE sample, float volume) {
    Sample3DState* s = (Sample3DState*)sample;
    s->volume = volume;
    EM_ASM({
        let s = window.AILBridge.getSample($0);
        s.volume = $1;
        if (s.gainNode) {
            s.gainNode.gain.value = $1;
        }
    }, s->id, volume);
}

void __stdcall AIL_end_3D_sample(H3DSAMPLE sample) {
    AIL_stop_3D_sample(sample);
}

void __stdcall AIL_resume_3D_sample(H3DSAMPLE sample) {
    AIL_start_3D_sample(sample);
}

void __stdcall AIL_stop_3D_sample(H3DSAMPLE sample) {
    Sample3DState* s = (Sample3DState*)sample;
    s->is_playing = false;
    EM_ASM({
        let s = window.AILBridge.getSample($0);
        if (s.source) {
            try { s.source.stop(); } catch(e) {}
            s.source = null;
        }
        s.playWhenReady = false;
    }, s->id);
}

void __stdcall AIL_start_3D_sample(H3DSAMPLE sample) {
    Sample3DState* s = (Sample3DState*)sample;
    s->is_playing = true;
    EM_ASM({
        window.AILBridge.play($0, 1, $1);
    }, s->id, s->loop_count == 0 || s->loop_count > 1);
}

unsigned int __stdcall AIL_3D_sample_loop_count(H3DSAMPLE sample) {
    Sample3DState* s = (Sample3DState*)sample;
    return s->loop_count;
}

void __stdcall AIL_set_3D_sample_offset(H3DSAMPLE sample, unsigned int offset) {
}

int __stdcall AIL_3D_sample_length(H3DSAMPLE sample) {
    return 0;
}

unsigned int __stdcall AIL_3D_sample_offset(H3DSAMPLE sample) {
    return 0;
}

int __stdcall AIL_3D_sample_playback_rate(H3DSAMPLE sample) {
    Sample3DState* s = (Sample3DState*)sample;
    return s->playback_rate;
}

void __stdcall AIL_set_3D_sample_playback_rate(H3DSAMPLE sample, int playback_rate) {
    Sample3DState* s = (Sample3DState*)sample;
    s->playback_rate = playback_rate;
}

int __stdcall AIL_set_3D_sample_file(H3DSAMPLE sample, const void* file_image) {
    Sample3DState* s = (Sample3DState*)sample;
    GX_TRACE_LOG("[web_audio_bridge] AIL_set_3D_sample_file: sample=%p, id=%d, file_image=%p\n", (void*)sample, s ? s->id : -1, file_image);
    s->file_image = file_image;
    if (file_image) {
        EM_ASM({
            window.AILBridge.setSampleFile($0, $1, 1);
        }, s->id, file_image);
    }
    return 1;
}

HPROVIDER __stdcall AIL_set_sample_processor(HSAMPLE sample, SAMPLESTAGE pipeline_stage, HPROVIDER provider) {
    return 0;
}

void __stdcall AIL_set_filter_sample_preference(HSAMPLE sample, const char* name, const void* val) {
}

void __stdcall AIL_release_sample_handle(HSAMPLE sample) {
    SampleState* s = (SampleState*)sample;
    s->allocated = false;
}

void __stdcall AIL_close_3D_provider(HPROVIDER lib) {
}

int __stdcall AIL_set_preference(unsigned int number, int value) {
    return 1;
}

int __stdcall AIL_waveOutOpen(HDIGDRIVER* driver, LPHWAVEOUT* waveout, int id, LPWAVEFORMAT format) {
    return 0;
}

void __stdcall AIL_waveOutClose(HDIGDRIVER driver) {
}

void __stdcall AIL_set_3D_sample_loop_count(H3DSAMPLE sample, unsigned int count) {
    Sample3DState* s = (Sample3DState*)sample;
    s->loop_count = count;
}

void __stdcall AIL_set_stream_playback_rate(HSTREAM stream, int rate) {
}

int __stdcall AIL_stream_playback_rate(HSTREAM stream) {
    return 22050;
}

void __stdcall AIL_stream_ms_position(HSTREAM sample, S32* total_milliseconds, S32* current_milliseconds) {
    StreamState* s = (StreamState*)sample;
    EM_ASM({
        window.AILBridge.getStreamPos($0, $1, $2);
    }, s->id, total_milliseconds, current_milliseconds);
}

void __stdcall AIL_set_stream_ms_position(HSTREAM stream, int pos) {
    StreamState* s = (StreamState*)stream;
    EM_ASM({
        let s = window.AILBridge.getSample($0 + 1000);
        if (s.audio) {
            try { s.audio.currentTime = $1 / 1000.0; } catch(e) {}
        }
    }, s->id, pos);
}

int __stdcall AIL_stream_loop_count(HSTREAM stream) {
    StreamState* s = (StreamState*)stream;
    return s->loop_count;
}

void __stdcall AIL_set_stream_loop_block(HSTREAM stream, int loop_start, int loop_end) {
}

void __stdcall AIL_set_stream_loop_count(HSTREAM stream, int count) {
    StreamState* s = (StreamState*)stream;
    s->loop_count = count;
}

int __stdcall AIL_stream_volume(HSTREAM stream) {
    StreamState* s = (StreamState*)stream;
    return (int)(s->volume * 127.0f);
}

void __stdcall AIL_set_stream_volume(HSTREAM stream, int volume) {
    StreamState* s = (StreamState*)stream;
    s->volume = (float)volume / 127.0f;
    EM_ASM({
        let s = window.AILBridge.getSample($0 + 1000);
        s.volume = $1;
        if (s.gainNode) {
            s.gainNode.gain.value = $1;
        }
    }, s->id, s->volume);
}

int __stdcall AIL_stream_pan(HSTREAM stream) {
    StreamState* s = (StreamState*)stream;
    return (int)(s->pan * 127.0f);
}

void __stdcall AIL_set_stream_pan(HSTREAM stream, int pan) {
    StreamState* s = (StreamState*)stream;
    s->pan = (float)pan / 127.0f;
    EM_ASM({
        let s = window.AILBridge.getSample($0 + 1000);
        s.pan = $1;
        if (s.pannerNode && s.pannerNode.pan) {
            s.pannerNode.pan.value = ($1 - 0.5) * 2.0;
        }
    }, s->id, s->pan);
}

void __stdcall AIL_close_stream(HSTREAM stream) {
    StreamState* s = (StreamState*)stream;
    s->allocated = false;
    EM_ASM({
        let s = window.AILBridge.getSample($0 + 1000);
        if (s.audio) {
            try { s.audio.pause(); } catch(e) {}
            s.audio = null;
        }
        if (s.mediaSourceNode) {
            try { s.mediaSourceNode.disconnect(); } catch(e) {}
            s.mediaSourceNode = null;
        }
        if (s.objectURL) {
            try { URL.revokeObjectURL(s.objectURL); } catch(e) {}
            s.objectURL = null;
        }
        if (s.source) {
            try { s.source.stop(); } catch(e) {}
            s.source = null;
        }
        delete window.AILBridge.samples[$0 + 1000];
    }, s->id);
}

void __stdcall AIL_pause_stream(HSTREAM stream, int onoff) {
    StreamState* s = (StreamState*)stream;
    if (onoff) {
        EM_ASM({
            let s = window.AILBridge.getSample($0 + 1000);
            if (s.audio) {
                try { s.audio.pause(); } catch(e) {}
            } else if (s.source) {
                try { s.source.stop(); } catch(e) {}
                s.source = null;
            }
        }, s->id);
    } else {
        if (s->is_playing) {
            bool has_audio = EM_ASM_INT({
                let s = window.AILBridge.getSample($0 + 1000);
                if (s.audio) {
                    s.audio.play().catch(e => {});
                    return 1;
                }
                return 0;
            }, s->id);
            if (!has_audio) {
                AIL_start_stream(stream);
            }
        }
    }
}

AIL_stream_callback __stdcall AIL_register_stream_callback(HSTREAM stream, AIL_stream_callback callback) {
    StreamState* s = (StreamState*)stream;
    AIL_stream_callback old = s->callback;
    s->callback = callback;
    return old;
}

AIL_3dsample_callback __stdcall AIL_register_3D_EOS_callback(H3DSAMPLE sample, AIL_3dsample_callback EOS) {
    Sample3DState* s = (Sample3DState*)sample;
    AIL_3dsample_callback old = s->eos_callback;
    s->eos_callback = EOS;
    return old;
}

AIL_sample_callback __stdcall AIL_register_EOS_callback(HSAMPLE sample, AIL_sample_callback EOS) {
    SampleState* s = (SampleState*)sample;
    AIL_sample_callback old = s->eos_callback;
    s->eos_callback = EOS;
    return old;
}

void __stdcall AIL_start_stream(HSTREAM stream) {
    StreamState* s = (StreamState*)stream;
    GX_TRACE_LOG("[web_audio_bridge] AIL_start_stream: stream=%p, id=%d, filename='%s', loop_count=%d\n",
           (void*)stream, s ? s->id : -1, s ? s->filename : "NULL", s ? s->loop_count : -1);
    s->is_playing = true;
    EM_ASM({
        window.AILBridge.play($0 + 1000, 0, $1);
    }, s->id, s->loop_count == 0 || s->loop_count > 1);
}

HSTREAM __stdcall AIL_open_stream_by_sample(HDIGDRIVER driver, HSAMPLE sample, const char* file_name, int mem) {
    return 0;
}

void __stdcall AIL_set_sample_playback_rate(HSAMPLE sample, int playback_rate) {
    SampleState* s = (SampleState*)sample;
    s->playback_rate = playback_rate;
}

int __stdcall AIL_sample_playback_rate(HSAMPLE sample) {
    SampleState* s = (SampleState*)sample;
    return s->playback_rate;
}

void __stdcall AIL_sample_ms_position(HSAMPLE sample, S32* total_ms, S32* current_ms) {
}

void __stdcall AIL_set_sample_ms_position(HSAMPLE sample, int pos) {
}

int __stdcall AIL_sample_loop_count(HSAMPLE sample) {
    SampleState* s = (SampleState*)sample;
    return s->loop_count;
}

void __stdcall AIL_set_sample_loop_count(HSAMPLE sample, int count) {
    SampleState* s = (SampleState*)sample;
    s->loop_count = count;
}

int __stdcall AIL_sample_volume(HSAMPLE sample) {
    SampleState* s = (SampleState*)sample;
    return (int)(s->volume * 127.0f);
}

void __stdcall AIL_set_sample_volume(HSAMPLE sample, int volume) {
    SampleState* s = (SampleState*)sample;
    s->volume = (float)volume / 127.0f;
    EM_ASM({
        let s = window.AILBridge.getSample($0);
        s.volume = $1;
        if (s.gainNode) {
            s.gainNode.gain.value = $1;
        }
    }, s->id, s->volume);
}

int __stdcall AIL_sample_pan(HSAMPLE sample) {
    SampleState* s = (SampleState*)sample;
    return (int)(s->pan * 127.0f);
}

void __stdcall AIL_set_sample_pan(HSAMPLE sample, int pan) {
    SampleState* s = (SampleState*)sample;
    s->pan = (float)pan / 127.0f;
    EM_ASM({
        let s = window.AILBridge.getSample($0);
        s.pan = $1;
        if (s.pannerNode && s.pannerNode.pan) {
            s.pannerNode.pan.value = ($1 - 0.5) * 2.0;
        }
    }, s->id, s->pan);
}

void __stdcall AIL_end_sample(HSAMPLE sample) {
    AIL_stop_sample(sample);
}

void __stdcall AIL_resume_sample(HSAMPLE sample) {
    AIL_start_sample(sample);
}

void __stdcall AIL_stop_sample(HSAMPLE sample) {
    SampleState* s = (SampleState*)sample;
    s->is_playing = false;
    EM_ASM({
        let s = window.AILBridge.getSample($0);
        if (s.source) {
            try { s.source.stop(); } catch(e) {}
            s.source = null;
        }
        s.playWhenReady = false;
    }, s->id);
}

void __stdcall AIL_start_sample(HSAMPLE sample) {
    SampleState* s = (SampleState*)sample;
    GX_TRACE_LOG("[web_audio_bridge] AIL_start_sample: sample=%p, id=%d, loop_count=%d\n",
           (void*)sample, s ? s->id : -1, s ? s->loop_count : -1);
    s->is_playing = true;
    EM_ASM({
        window.AILBridge.play($0, 0, $1);
    }, s->id, s->loop_count == 0 || s->loop_count > 1);
}

void __stdcall AIL_init_sample(HSAMPLE sample) {
    SampleState* s = (SampleState*)sample;
    s->file_image = nullptr;
    s->volume = 1.0f;
    s->pan = 0.5f;
    s->loop_count = 1;
    s->is_playing = false;
}

int __stdcall AIL_set_named_sample_file(
    HSAMPLE sample, const char* file_name, const void* file_image, int file_size, int block) {
    return AIL_set_sample_file(sample, file_image, block);
}

void __stdcall AIL_set_3D_sample_effects_level(H3DSAMPLE sample, float effect_level) {
}

void __stdcall AIL_set_3D_sample_distances(H3DSAMPLE sample, float max_dist, float min_dist) {
    Sample3DState* s = (Sample3DState*)sample;
    s->max_dist = max_dist;
    s->min_dist = min_dist;
    EM_ASM({
        let s = window.AILBridge.getSample($0);
        s.max_dist = $1;
        s.min_dist = $2;
        if (s.pannerNode) {
            s.pannerNode.maxDistance = $1;
            s.pannerNode.refDistance = $2;
        }
    }, s->id, max_dist, min_dist);
}

void __stdcall AIL_set_3D_velocity_vector(H3DSAMPLE sample, float x, float y, float z) {
}

void __stdcall AIL_set_3D_position(H3DPOBJECT obj, float X, float Y, float Z) {
    // Translate coordinates: X_wa = X, Y_wa = Z, Z_wa = -Y
    float wa_x = X;
    float wa_y = Z;
    float wa_z = -Y;
    if (obj == LISTENER_HANDLE) {
        EM_ASM({
            if (window.AILBridge.audioContext) {
                let listener = window.AILBridge.audioContext.listener;
                if (listener.positionX) {
                    listener.positionX.value = $0;
                    listener.positionY.value = $1;
                    listener.positionZ.value = $2;
                } else {
                    listener.setPosition($0, $1, $2);
                }
            }
        }, wa_x, wa_y, wa_z);
    } else {
        Sample3DState* s = (Sample3DState*)obj;
        s->x = wa_x; s->y = wa_y; s->z = wa_z;
        EM_ASM({
            let s = window.AILBridge.getSample($0);
            s.x = $1; s.y = $2; s.z = $3;
            if (s.pannerNode && s.pannerNode.positionX) {
                s.pannerNode.positionX.value = $1;
                s.pannerNode.positionY.value = $2;
                s.pannerNode.positionZ.value = $3;
            }
        }, s->id, wa_x, wa_y, wa_z);
    }
}

void __stdcall AIL_set_3D_orientation(
    H3DPOBJECT obj, float X_face, float Y_face, float Z_face, float X_up, float Y_up, float Z_up) {
    if (obj == LISTENER_HANDLE) {
        // Project face vector onto Generals X-Y plane (Z_face = 0)
        float proj_x = X_face;
        float proj_y = Y_face;
        float len = sqrtf(proj_x * proj_x + proj_y * proj_y);
        if (len > 0.0001f) {
            proj_x /= len;
            proj_y /= len;
        } else {
            proj_x = 0.0f;
            proj_y = 1.0f;
        }
        
        // Map to Web Audio coordinates: X_wa = X, Y_wa = Z, Z_wa = -Y
        float wa_face_x = proj_x;
        float wa_face_y = 0.0f;
        float wa_face_z = -proj_y;
        
        // Hardcode Web Audio listener's Up vector to standard upright (0, 1, 0)
        float wa_up_x = 0.0f;
        float wa_up_y = 1.0f;
        float wa_up_z = 0.0f;

        EM_ASM({
            if (window.AILBridge.audioContext) {
                let listener = window.AILBridge.audioContext.listener;
                if (listener.forwardX) {
                    listener.forwardX.value = $0;
                    listener.forwardY.value = $1;
                    listener.forwardZ.value = $2;
                    listener.upX.value = $3;
                    listener.upY.value = $4;
                    listener.upZ.value = $5;
                } else {
                    listener.setOrientation($0, $1, $2, $3, $4, $5);
                }
            }
        }, wa_face_x, wa_face_y, wa_face_z, wa_up_x, wa_up_y, wa_up_z);
    }
}

int __stdcall AIL_WAV_info(const void* data, AILSOUNDINFO* info) {
    WavHeaderInfo wav;
    if (!parse_wav(data, wav)) return 0;
    info->format = wav.format;
    info->data_ptr = wav.data_ptr;
    info->data_len = wav.data_len;
    info->rate = wav.sample_rate;
    info->bits = wav.bits_per_sample;
    info->channels = wav.channels;
    info->initial_ptr = data;
    info->block_size = wav.block_align;
    if (wav.format == 1) {
        info->samples = wav.data_len / (wav.channels * (wav.bits_per_sample / 8));
    } else {
        info->samples = wav.data_len * 2;
    }
    return 1;
}

void __stdcall AIL_stop_timer(HTIMER timer) {
}

void __stdcall AIL_release_timer_handle(HTIMER timer) {
}

void __stdcall AIL_shutdown(void) {
}

int __stdcall AIL_enumerate_filters(HPROENUM* next, HPROVIDER* dest, char** name) {
    return 0;
}

void __stdcall AIL_set_file_callbacks(AIL_file_open_callback opencb, AIL_file_close_callback closecb,
    AIL_file_seek_callback seekcb, AIL_file_read_callback readcb) {
}

void __stdcall AIL_release_3D_sample_handle(H3DSAMPLE sample) {
    Sample3DState* s = (Sample3DState*)sample;
    s->allocated = false;
}

H3DSAMPLE __stdcall AIL_allocate_3D_sample_handle(HPROVIDER lib) {
    for (int i = 0; i < MAX_SAMPLES; i++) {
        if (!g_samples3D[i].allocated) {
            g_samples3D[i].allocated = true;
            g_samples3D[i].id = i + 500; // offset 3D sample IDs to prevent collision
            g_samples3D[i].is_playing = false;
            g_samples3D[i].volume = 1.0f;
            g_samples3D[i].max_dist = 1000.0f;
            g_samples3D[i].min_dist = 10.0f;
            g_samples3D[i].x = 0.0f; g_samples3D[i].y = 0.0f; g_samples3D[i].z = 0.0f;
            g_samples3D[i].loop_count = 1;
            g_samples3D[i].playback_rate = 22050;
            g_samples3D[i].eos_callback = nullptr;
            g_samples3D[i].file_image = nullptr;
            return (H3DSAMPLE)&g_samples3D[i];
        }
    }
    return nullptr;
}

void __stdcall AIL_set_3D_user_data(H3DPOBJECT obj, unsigned int index, void *value) {
}

void __stdcall AIL_unlock(void) {
}

void __stdcall AIL_lock(void) {
}

void __stdcall AIL_set_3D_speaker_type(HPROVIDER lib, int speaker_type) {
}

void __stdcall AIL_close_3D_listener(H3DPOBJECT listener) {
}

int __stdcall AIL_enumerate_3D_providers(HPROENUM* next, HPROVIDER* dest, char** name) {
    if (next && *next == 0) {
        *dest = (HPROVIDER)1;
        *name = (char*)"Miles Fast 2D Positional Audio";
        *next = (HPROENUM)1;
        return 1;
    }
    return 0;
}

M3DRESULT __stdcall AIL_open_3D_provider(HPROVIDER lib) {
    return 0;
}

char* __stdcall AIL_last_error(void) {
    return (char*)"No error";
}

H3DPOBJECT __stdcall AIL_open_3D_listener(HPROVIDER lib) {
    return LISTENER_HANDLE;
}

void *__stdcall AIL_3D_user_data(H3DSAMPLE sample, unsigned int index) {
    return nullptr;
}

void *__stdcall AIL_sample_user_data(HSAMPLE sample, unsigned int index) {
    return nullptr;
}

HSAMPLE __stdcall AIL_allocate_sample_handle(HDIGDRIVER dig) {
    for (int i = 0; i < MAX_SAMPLES; i++) {
        if (!g_samples[i].allocated) {
            g_samples[i].allocated = true;
            g_samples[i].id = i + 1;
            g_samples[i].is_playing = false;
            g_samples[i].volume = 1.0f;
            g_samples[i].pan = 0.5f;
            g_samples[i].loop_count = 1;
            g_samples[i].playback_rate = 22050;
            g_samples[i].eos_callback = nullptr;
            g_samples[i].file_image = nullptr;
            return (HSAMPLE)&g_samples[i];
        }
    }
    return nullptr;
}

void __stdcall AIL_set_sample_user_data(HSAMPLE sample, unsigned int index, void *value) {
}

int __stdcall AIL_decompress_ADPCM(const AILSOUNDINFO *info, void **outdata, unsigned long *outsize) {
    if (!info || !info->data_ptr || info->data_len == 0) return 0;

    uint32_t block_align = info->block_size;
    if (block_align == 0) {
        if (info->channels == 1) block_align = 512;
        else block_align = 1024;
    }
    uint32_t num_blocks = info->data_len / block_align;
    uint32_t samples_per_block = 0;
    if (info->channels == 1) {
        samples_per_block = 1 + (block_align - 4) * 2;
    } else {
        samples_per_block = 1 + ((block_align - 8) / 16) * 16;
    }

    uint32_t total_samples = num_blocks * samples_per_block;
    uint32_t pcm_data_size = total_samples * info->channels * sizeof(int16_t);
    uint32_t total_wav_size = sizeof(WavHeader) + pcm_data_size;

    uint8_t* wav_buf = (uint8_t*)malloc(total_wav_size);
    if (!wav_buf) return 0;

    WavHeader* hdr = (WavHeader*)wav_buf;
    memcpy(hdr->riff, "RIFF", 4);
    hdr->riff_size = total_wav_size - 8;
    memcpy(hdr->wave, "WAVE", 4);
    memcpy(hdr->fmt, "fmt ", 4);
    hdr->fmt_size = 16;
    hdr->format_tag = 1; // PCM
    hdr->channels = info->channels;
    hdr->sample_rate = info->rate;
    hdr->byte_rate = info->rate * info->channels * sizeof(int16_t);
    hdr->block_align = info->channels * sizeof(int16_t);
    hdr->bits = 16;
    memcpy(hdr->data, "data", 4);
    hdr->data_size = pcm_data_size;

    int16_t* dst = (int16_t*)(wav_buf + sizeof(WavHeader));
    const uint8_t* src = (const uint8_t*)info->data_ptr;

    for (uint32_t b = 0; b < num_blocks; b++) {
        if (info->channels == 1) {
            decode_block_mono(src + b * block_align, block_align, dst + b * samples_per_block);
        } else {
            decode_block_stereo(src + b * block_align, block_align, dst + b * samples_per_block * 2);
        }
    }

    *outdata = wav_buf;
    *outsize = total_wav_size;
    return 1;
}

void __stdcall AIL_get_DirectSound_info(HSAMPLE sample, AILLPDIRECTSOUND *lplpDS, AILLPDIRECTSOUNDBUFFER *lplpDSB) {
}

void __stdcall AIL_mem_free_lock(void *ptr) {
    if (ptr) free(ptr);
}

HSTREAM __stdcall AIL_open_stream(HDIGDRIVER dig, const char *filename, int stream_mem) {
    GX_TRACE_LOG("[web_audio_bridge] AIL_open_stream: filename='%s', stream_mem=%d\n", filename, stream_mem);
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!g_streams[i].allocated) {
            g_streams[i].allocated = true;
            g_streams[i].id = i + 1;
            strncpy(g_streams[i].filename, filename, sizeof(g_streams[i].filename) - 1);
            g_streams[i].volume = 1.0f;
            g_streams[i].pan = 0.5f;
            g_streams[i].loop_count = 1;
            g_streams[i].callback = nullptr;
            g_streams[i].is_playing = false;

            uint32_t size = 0;
            char* buf = WebAudioBridge_ReadEntireFile(filename, &size);
            GX_TRACE_LOG("[web_audio_bridge] AIL_open_stream: WebAudioBridge_ReadEntireFile returned %p, size = %u\n", (void*)buf, size);
            if (buf) {
                EM_ASM({
                    window.AILBridge.setStreamFile($0, $1, $2);
                }, g_streams[i].id, buf, size);
                delete[] buf;
            } else {
                printf("[web_audio_bridge] AIL_open_stream WARNING: WebAudioBridge_ReadEntireFile failed or returned NULL\n");
            }
            return (HSTREAM)&g_streams[i];
        }
    }
    printf("[web_audio_bridge] AIL_open_stream ERROR: No free stream slot available\n");
    return nullptr;
}

void __stdcall AIL_quick_unload(HAUDIO audio) {
}

HAUDIO __stdcall AIL_quick_load_and_play(const char *filename, unsigned int loop_count, int wait_request) {
    return nullptr;
}

void __stdcall AIL_quick_set_volume(HAUDIO audio, float volume, float extravol) {
}

int __stdcall AIL_quick_startup(
    int use_digital, int use_MIDI, unsigned int output_rate, int output_bits, int output_channels) {
    GX_TRACE_LOG("[web_audio_bridge] AIL_quick_startup: use_digital=%d, use_MIDI=%d, rate=%u, bits=%d, channels=%d\n",
           use_digital, use_MIDI, output_rate, output_bits, output_channels);
#if GX_TRACE_LOGGING
    // Debug builds only: let the AILBridge EM_JS console traces through.
    EM_ASM({ window.gxAudioVerbose = true; });
#endif
    AILBridge_init();
    EM_ASM({
        window.AILBridge.init();
    });
    return 1;
}

void __stdcall AIL_quick_handles(HDIGDRIVER *pdig, HMDIDRIVER *pmdi, HDLSDEVICE *pdls) {
    if (pdig) *pdig = (HDIGDRIVER)1;
    if (pmdi) *pmdi = (HMDIDRIVER)1;
    if (pdls) *pdls = (HDLSDEVICE)1;
}

void __stdcall AIL_sample_volume_pan(HSAMPLE sample, float *volume, float *pan) {
    SampleState* s = (SampleState*)sample;
    if (volume) *volume = s->volume;
    if (pan) *pan = s->pan;
}

void __stdcall AIL_set_3D_sample_occlusion(H3DSAMPLE sample, float occlusion) {
}

char *__stdcall AIL_set_redist_directory(const char *dir) {
    return nullptr;
}

int __stdcall AIL_set_sample_file(HSAMPLE sample, const void *file_image, int block) {
    SampleState* s = (SampleState*)sample;
    GX_TRACE_LOG("[web_audio_bridge] AIL_set_sample_file: sample=%p, id=%d, file_image=%p, block=%d\n",
           (void*)sample, s ? s->id : -1, file_image, block);
    s->file_image = file_image;
    if (file_image) {
        EM_ASM({
            window.AILBridge.setSampleFile($0, $1, 0);
        }, s->id, file_image);
    }
    return 1;
}

void __stdcall AIL_set_sample_volume_pan(HSAMPLE sample, float volume, float pan) {
    SampleState* s = (SampleState*)sample;
    s->volume = volume;
    s->pan = pan;
    EM_ASM({
        let s = window.AILBridge.getSample($0);
        s.volume = $1;
        s.pan = $2;
        if (s.gainNode) {
            s.gainNode.gain.value = $1;
        }
        if (s.pannerNode && s.pannerNode.pan) {
            s.pannerNode.pan.value = ($2 - 0.5) * 2.0;
        }
    }, s->id, volume, pan);
}

void __stdcall AIL_set_stream_volume_pan(HSTREAM stream, float volume, float pan) {
    StreamState* s = (StreamState*)stream;
    s->volume = volume;
    s->pan = pan;
    EM_ASM({
        let s = window.AILBridge.getSample($0 + 1000);
        s.volume = $1;
        s.pan = $2;
        if (s.gainNode) {
            s.gainNode.gain.value = $1;
        }
        if (s.pannerNode && s.pannerNode.pan) {
            s.pannerNode.pan.value = ($2 - 0.5) * 2.0;
        }
    }, s->id, volume, pan);
}

void __stdcall AIL_stream_volume_pan(HSTREAM stream, float *volume, float *pan) {
    StreamState* s = (StreamState*)stream;
    if (volume) *volume = s->volume;
    if (pan) *pan = s->pan;
}

int __stdcall AIL_startup(void) {
    GX_TRACE_LOG("[web_audio_bridge] AIL_startup called\n");
#if GX_TRACE_LOGGING
    // Debug builds only: let the AILBridge EM_JS console traces through.
    EM_ASM({ window.gxAudioVerbose = true; });
#endif
    AILBridge_init();
    EM_ASM({
        window.AILBridge.init();
    });
    return 1;
}

unsigned long __stdcall AIL_get_timer_highest_delay(void) {
    return 0;
}

} // extern "C"
