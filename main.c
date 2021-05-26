#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "lame.h"
#include "noise_suppression.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#define BUF_LEN 160


NsHandle *_ns = 0;
int16_t sIn[BUF_LEN];
int16_t sOut[BUF_LEN];
lame_global_flags *glf_ns = 0;


int processStream(int16_t *buffer, int length) {
  if (_ns) {
    if (length <= 0) {
        return -12;
    }

    // printf("Process Stream malloc sIn/sOut OK\n");

    int count = 0;
    int c = 0;
    int i;
    int j;
    for (i = 0; i < length; i += BUF_LEN) {
      if (i + BUF_LEN <= length)
          c = BUF_LEN;
      else
          c = length - i;
      // printf("Process Stream c: %d\n", c);

      memcpy(sIn, buffer + i, sizeof(int16_t) * c);
      count += c;
      // printf("Process Stream count: %d\n", count);

      const int16_t* pIn = (int16_t *)(&sIn);
      int16_t *pOut = (int16_t *)(&sOut);
      const int16_t* const* speechFrame = &pIn;
      int16_t* const* outFrame = &pOut;
      
      WebRtcNs_Analyze(_ns, pIn);
      // printf("Process Stream WebRtcNs_Analyze: OK\n");
      WebRtcNs_Process(_ns, speechFrame, 1, outFrame);
      // printf("Process Stream WebRtcNs_Process: OK\n");

      memcpy(buffer + i, sOut, sizeof(int16_t) * c);
      // for (j = 0; j < c; j++) buffer[i + j] = sIn[j];
      // printf("Process Stream memcpy sOut: OK\n");
      memset(sOut, 0, BUF_LEN);
    }
    return count;
  } else {
    return -11;
  }
}

int64_t writeDataToWavFile(uint32_t sampleRate, uint32_t channels,
                           const int16_t *buffer, uint64_t sampleCount,
                           const char *outName) {
  printf("Start Write Wav file : %s\n", outName);
  drwav_data_format format = {};
  format.container = drwav_container_riff;
  format.format = DR_WAVE_FORMAT_PCM;
  format.channels = channels;
  format.sampleRate = (drwav_uint32)sampleRate;
  format.bitsPerSample = 16;
  drwav wav;
  drwav_init_file_write(&wav, outName, &format, 0);
  printf("Write Wav file sampleRate: %d\n", sampleRate);
  drwav_uint64 written = drwav_write_pcm_frames(&wav, sampleCount, buffer);
  drwav_uninit(&wav);
  if (written != sampleCount) {
    printf("Write Wav file fail.\n");
    return -1;
  } else {
    printf("Write Wav file success: %ld\n", written);
    return written;
  }
}

int64_t writeDataToMp3File(uint32_t sampleRate, uint32_t channels, int32_t outBitrate,
                           const int16_t *buffer, uint64_t length, const char *outName) {
  printf("NS Write Mp3 file: %s\n", outName);
  FILE *outfile = fopen(outName, "w+");

  glf_ns = lame_init();
  lame_set_in_samplerate(glf_ns, sampleRate);
  lame_set_num_channels(glf_ns, channels);
  lame_set_out_samplerate(glf_ns, sampleRate);
  lame_set_brate(glf_ns, outBitrate);
  lame_set_quality(glf_ns, 7);
  lame_init_params(glf_ns);
  printf("NS Write Mp3 file lame_init completed ...\n");

  printf("NS Write Mp3 file start ...\n");
  uint32_t mp3Size = (int) (7200 + (length * channels * 1.25));
  unsigned char *mp3Buf = (unsigned char *)(malloc(mp3Size * sizeof(unsigned char)));

  int64_t result = lame_encode_buffer(glf_ns, buffer, buffer, length, (unsigned char *) mp3Buf, mp3Size);
  fwrite(mp3Buf, sizeof(unsigned char), result, outfile);
  printf("NS Encode Mp3 file count: %ld\n", result);

  result = lame_encode_flush(glf_ns, (unsigned char *) mp3Buf, mp3Size);
  printf("NS Encode Mp3 file flush: %ld\n", result);

  fwrite(mp3Buf, sizeof(unsigned char), result, outfile);
  free(mp3Buf);
  fclose(outfile);
  printf("NS Encode Mp3 file close: %s\n", outName);

  lame_close(glf_ns);
  glf_ns = 0;
  printf("NS Encode Mp3 file lame_close\n");
  return result;
}

int64_t processWavFile(const char *inName, const char *outName, int mode) {
  uint32_t sampleRate = 0;
  uint64_t sampleCount = 0;
  unsigned int channels = 0;
  printf("NS Start Read Wav file : %s\n", inName);
  int16_t *buffer = drwav_open_file_and_read_pcm_frames_s16(inName, &channels, &sampleRate, &sampleCount, 0);
  if (buffer == 0) {
    printf("NS Read Wav file fail : %s\n", inName);
    return -1;
  }
  printf("NS Read Wav file Completed : sampleRate=%d, channels=%d, count=%ld\n", sampleRate, channels, sampleCount);
  _ns = WebRtcNs_Create();
  if (_ns && WebRtcNs_Init(_ns, sampleRate) != 0) _ns = 0;
  if (_ns && WebRtcNs_set_policy(_ns, mode) != 0) _ns = 0;
  int res = processStream(buffer, sampleCount * channels);
  if (_ns) WebRtcNs_Free(_ns);
  _ns = 0;
  if (res < 0) {
    printf("NS Wav file Fail: %d\n", res);
    return -2;
  }
  printf("NS Wav file Completed: %ld\n", sampleCount);
  return  writeDataToWavFile(sampleRate, channels, buffer, sampleCount, outName);
}

int64_t processMp3File(const char *inName, const char *outName, int mode) {
  drmp3 mp3;
  if (!drmp3_init_file(&mp3, inName, 0)) {
    printf("NS Mp3 file init fail\n");
    return -1;
  }

  int32_t bitrate = 128; // mp3.frameInfo.bitrate_kbps;
  drmp3_uint32 channels = mp3.channels;
  drmp3_uint32 sampleRate = mp3.sampleRate;
  printf("NS Mp3 file read info: channels:%d, sampleRate:%d, bitrate:%d\n", channels, sampleRate, bitrate);
  drmp3_uint64 sampleCount = 0;
  drmp3_int16 *buffer = drmp3__full_read_and_close_s16(&mp3, 0, &sampleCount);
  drmp3_uint64 length = sampleCount;
  printf("NS Mp3 file read size: %ld\n", length);

  if (buffer == 0) {
    return - 2;
  }

  int64_t res = 0;
  if (channels == 1) {
    _ns = WebRtcNs_Create();
    if (_ns && WebRtcNs_Init(_ns, sampleRate) != 0) _ns = 0;
    if (_ns && WebRtcNs_set_policy(_ns, mode) != 0) _ns = 0;
    res = processStream(buffer, length);
    if (_ns) WebRtcNs_Free(_ns);
    _ns = 0;
    if (res < 0) {
        printf("NS Mp3 file Fail: %ld\n", res);
        free(buffer);
        return -3;
    }
    printf("NS Mp3 file processStream completed: %ld\n", length);
    res = writeDataToMp3File(sampleRate, channels, bitrate, buffer, length, outName);
    if (res < 0) {
      printf("NS Mp3 file write to file Fail: %ld\n", res);
      free(buffer);
      return -4;
    }
  } else if (channels == 2) {
    drmp3_int16 *lBuf = (drmp3_int16 *)(malloc(length * channels * sizeof(drmp3_int16)));
    int i;
    for (i = 0; i < length; i++) lBuf[i] = buffer[i * channels];
    printf("NS Mp3 file LBuf copy completed: %ld\n", length);
    _ns = WebRtcNs_Create();
    if (_ns && WebRtcNs_Init(_ns, sampleRate) != 0) _ns = 0;
    if (_ns && WebRtcNs_set_policy(_ns, mode) != 0) _ns = 0;
    res = processStream(lBuf, length);
    if (_ns) WebRtcNs_Free(_ns);
    _ns = 0;
    if (res < 0) {
      printf("NS Mp3 file Fail: %ld\n", res);
      free(lBuf);
      free(buffer);
      return -5;
    }
    printf("NS Mp3 file processStream completed: %ld\n", length);
    res = writeDataToMp3File(sampleRate, channels, bitrate, lBuf, length, outName);
    free(lBuf);
    if (res < 0) {
      printf("NS Mp3 file write to file Fail: %ld\n", res);
        free(buffer);
        return -6;
    }
  }

  free(buffer);
  return res;
}

int main(int argc,char **argv) {
    if (argc <= 1) {
      printf("ns [wav/mp3/mp4 path] [ns level:0/1/2/3] ['[ffmpeg args]']\n");
      printf("     ns /home/ugc/xxx/8685.wav 2\n");
      printf("     ns /home/ugc/xxx/8685.mp3 2\n");
      printf("     ns /home/ugc/xxx/8685.mp4 2 '-af loudnorm=I=-16:tp=-1.5:LRA=11 -ar 44100'\n");
      return 0;
    }

    char *in_file = argc > 1 ? argv[1] : 0;
    int mode = argc > 2 ? atoi(argv[2]): 2;
    char *ff_args = argc > 3 ? argv[3] : 0;

    if (in_file == 0) return -1;
    if (strstr(in_file, ".mp4") != 0) {
      int in_length = strlen(in_file);
      int out_length = in_length + 3;
      char out_file[out_length];
      memset(out_file, '\0', out_length);
      strncpy(out_file, in_file, in_length - 4);
      sprintf(out_file, "%s.wav", out_file);
      char ext_ff_cmd[1024];
      sprintf(ext_ff_cmd, "ffmpeg -i %s -f wav -ar 44100 %s", in_file, out_file);
      int res = system(ext_ff_cmd);
      if (res == 0) {
        sprintf(in_file, "%s", out_file);
      } else {
        return -10;
      }
    }

    int in_length = strlen(in_file);
    int out_length = in_length + 3;
    char out_file[out_length];
    memset(out_file, '\0', out_length);
    strncpy(out_file, in_file, in_length - 4);
    printf("out_file: %s\n", out_file);

    if (strstr(in_file, ".wav") != 0) {
      sprintf(out_file, "%s_ns.wav", out_file);
    } else if (strstr(in_file, ".mp3") != 0) {
      sprintf(out_file, "%s_ns.mp3", out_file);
    }

    char ff_file[strlen(in_file) + 3];
    if (ff_args != 0 && strlen(ff_args) > 0) {
      char ext_ff_cmd[1024];
      memset(ff_file, '\0', out_length);
      strncpy(ff_file, in_file, in_length - 4);
      if (strstr(in_file, ".wav") != 0) {
        sprintf(ff_file, "%s_ff.wav", ff_file);
      } else if (strstr(in_file, ".mp3") != 0) {
        sprintf(ff_file, "%s_ff.mp3", ff_file);
      }
      printf("ff_file: %s\n", ff_file);
      sprintf(ext_ff_cmd, "ffmpeg -i %s %s %s", in_file, ff_args, ff_file);
      int res = system(ext_ff_cmd);
      if (res == 0) in_file = ff_file;
      printf("%s: %d\n", ext_ff_cmd, res);
    }

    if (strstr(in_file, ".wav") != 0) {
      int res = processWavFile(in_file, out_file, mode);
      printf("hello .wav: %s (ns:%d)-> %s : %d\n", in_file, mode, out_file, res);
      return 0;
    } else if (strstr(in_file, ".mp3") != 0) {
      int res = processMp3File(in_file, out_file, mode);
      printf("hello .mp3: %s (ns:%d)-> %s : %d\n", in_file, mode, out_file, res);
      return 0;
    }
    
    return -2;
}
