#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "elduderino.h"

#define DB_CO(g) ((g) > -90.0f ? powf(10.0f, (g) * 0.05f) : 0.0f)
#define N_VOICES (5)
#define PI (3.14159265358979323846)
#define TWO_PI (2 * PI)
#define PIOVR2 (PI/2)
#define ROOT2OVR2 (sqrt(2) * 0.5)
  
const float MIDI_KEYS[128] = {
  8.1757989156, 8.6619572180, 9.1770239974, 9.7227182413, 10.3008611535,
  10.9133822323, 11.5623257097, 12.2498573744, 12.9782717994,
  13.7500000000, 14.5676175474, 15.4338531643, 16.3515978313,
  17.3239144361, 18.3540479948, 19.4454364826, 20.6017223071,
  21.8267644646, 23.1246514195, 24.4997147489, 25.9565435987,
  27.5000000000, 29.1352350949, 30.8677063285, 32.7031956626,
  34.6478288721, 36.7080959897, 38.8908729653, 41.2034446141,
  43.6535289291, 46.2493028390, 48.9994294977, 51.9130871975,
  55.0000000000, 58.2704701898, 61.7354126570, 65.4063913251,
  69.2956577442, 73.4161919794, 77.7817459305, 82.4068892282,
  87.3070578583, 92.4986056779, 97.9988589954, 103.8261743950,
  110.0000000000, 116.5409403795, 123.4708253140, 130.8127826503,
  138.5913154884, 146.8323839587, 155.5634918610, 164.8137784564,
  174.6141157165, 184.9972113558, 195.9977179909, 207.6523487900,
  220.0000000000, 233.0818807590, 246.9416506281, 261.6255653006,
  277.1826309769, 293.6647679174, 311.1269837221, 329.6275569129,
  349.2282314330, 369.9944227116, 391.9954359817, 415.3046975799,
  440.0000000000, 466.1637615181, 493.8833012561, 523.2511306012,
  554.3652619537, 587.3295358348, 622.2539674442, 659.2551138257,
  698.4564628660, 739.9888454233, 783.9908719635, 830.6093951599,
  880.0000000000, 932.3275230362, 987.7666025122, 1046.5022612024,
  1108.7305239075, 1174.6590716696, 1244.5079348883, 1318.5102276515,
  1396.9129257320, 1479.9776908465, 1567.9817439270, 1661.2187903198,
  1760.0000000000, 1864.6550460724, 1975.5332050245, 2093.0045224048,
  2217.4610478150, 2349.3181433393, 2489.0158697766, 2637.0204553030,
  2793.8258514640, 2959.9553816931, 3135.9634878540, 3322.4375806396,
  3520.0000000000, 3729.3100921447, 3951.0664100490, 4186.0090448096,
  4434.9220956300, 4698.6362866785, 4978.0317395533, 5274.0409106059,
  5587.6517029281, 5919.9107633862, 6271.9269757080, 6644.8751612791,
  7040.0000000000, 7458.6201842894, 7902.1328200980, 8372.0180896192,
  8869.8441912599, 9397.2725733570, 9956.0634791066, 10548.0818212118,
  11175.3034058561, 11839.8215267723, 12543.8539514160
};

typedef enum {
  PORT_MIDI_IN = 0,
  PORT_GAIN,
  PORT_PANNING,
  PORT_ATTACK_LEVEL,
  PORT_ATTACK_TIME,
  PORT_SUSTAIN_LEVEL,
  PORT_DECAY_TIME,
  PORT_RELEASE_TIME,
  PORT_AUDIO_OUT_LEFT,
  PORT_AUDIO_OUT_RIGHT
} PortIndex;

typedef enum {
  ATTACK = 0,
  DECAY,
  SUSTAIN,
  RELEASE
} VoiceStatus;

typedef struct {
  int key;
  int velocity;
  float phase;
  float phase_increment;
  int sample_counter;
  float attack_duration;
  float attack_level;
  float decay_duration;
  float sustain_level;
  float release_duration;
  float envelope_level;
  float released_envelope_level;

  VoiceStatus status;
} Voice;

typedef struct {
  // necessary to generate a sin wave of the correct frequency
  double sample_rate;
  
  // port buffers
  const LV2_Atom_Sequence* control;
  const float* gain;
  const float* panning;
  const float* attack_level;
  const float* attack_time;
  const float* sustain_level;
  const float* decay_time;
  const float* release_time;
  float attack_duration;
  float decay_duration;
  float release_duration;
  float* out_left;
  float* out_right;

  Voice* voices[N_VOICES];

  // Features
  LV2_URID_Map* map;

  struct {
    LV2_URID midi_MidiEvent;
  } uris;
} ElDuderino;

static float
adsr(Voice* voice) {
  float level;

  switch(voice->status) {
  case SUSTAIN:
    level = voice->sustain_level;
    break;
  case ATTACK:
    if (voice->sample_counter > voice->attack_duration) {
      voice->status = DECAY;
      voice->sample_counter = 0;

      level = voice->attack_level;
    }
    else {
      level = voice->sample_counter * (voice->attack_level / voice->attack_duration);
    }

    break;
  case DECAY:
    if (voice->sample_counter > voice->decay_duration) {
      voice->status = SUSTAIN;
      voice->sample_counter = 0;

      level = voice->sustain_level;
    }
    else {
      level = ((voice->sustain_level - voice->attack_level) * (voice->sample_counter/voice->decay_duration) + voice->attack_level);
    }
    break;
  case RELEASE:
    if (voice->sample_counter > voice->release_duration) {
      voice->velocity = 0;

      level = 0;
    }
    else {
      level = voice->released_envelope_level * (voice->release_duration - voice->sample_counter)/voice->release_duration;
    }
    break;
  }

  voice->envelope_level = level;

  return level;
}

static float
tick_voice(Voice* voice) {
  float val = sin(voice->phase);

  voice->phase += voice->phase_increment;
  if ( voice->phase > TWO_PI ) {
    voice->phase -= TWO_PI;
  }

  val *= adsr(voice);;

  if (voice->status != SUSTAIN) {
    voice->sample_counter++;
  }

  return val;
}

static void
render_samples(uint32_t from, uint32_t to, ElDuderino* self) {
  float* const out_left  = self->out_left;
  float* const out_right = self->out_right;

  float gain = DB_CO(*(self->gain));

  float angle = (*(self->panning)) * PIOVR2 * 0.5;
  float pan_left  = ROOT2OVR2 * (cos(angle) - sin(angle));
  float pan_right = ROOT2OVR2 * (cos(angle) + sin(angle));

  for (uint32_t pos = from; pos < to; pos++) {
    out_left[pos] = 0;
    out_right[pos] = 0;

    for (int i_voice = 0; i_voice < N_VOICES; i_voice++) {
      Voice* voice = self->voices[i_voice];

      if (voice->velocity > 0) {
        float out = tick_voice(voice) * gain;

        out_left[pos]  += pan_left * out;
        out_right[pos] += pan_right * out;
      }
    }
  }
}

static Voice*
key_voice(ElDuderino* self, uint8_t key) {
  for (int i_voice = 0; i_voice < N_VOICES; i_voice++) {
    Voice* voice = self->voices[i_voice];

    if (voice->key == key && voice->velocity > 0) {
      return voice;
    }
  }

  return NULL;
}

static void
note_on(ElDuderino* self, uint8_t key, uint8_t velocity) {
  Voice* voice = key_voice(self, key);

  if (voice != NULL) {
    if (voice->status == RELEASE) {
      voice->status = ATTACK;
      voice->sample_counter = 0;
      voice->released_envelope_level = voice->envelope_level;
    }

    return;
  }

  for (int i_voice = 0; i_voice < N_VOICES; i_voice++) {
    Voice* voice = self->voices[i_voice];

    if (voice->velocity == 0) {
      float freq = MIDI_KEYS[key];
      voice->released_envelope_level = 0;
      voice->key = key;
      voice->velocity = velocity;
      voice->phase_increment = (freq * TWO_PI) / self->sample_rate;
      voice->sample_counter = 0;
      voice->status = ATTACK;

      voice->attack_level = *self->attack_level;
      voice->sustain_level = *self->sustain_level;
      voice->attack_duration = self->attack_duration;
      voice->decay_duration = self->decay_duration;
      voice->release_duration = self->release_duration;

      break;
    }
  }
}

static void
note_off(ElDuderino* self, uint8_t key) {
  Voice* voice = key_voice(self, key);

  if (voice != NULL) {
    voice->sample_counter = 0;
    voice->status = RELEASE;
    voice->released_envelope_level = voice->envelope_level;
  }
}

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
  LV2_URID_Map* map = NULL;
  for (int i = 0; features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_URID__map)) {
      map = (LV2_URID_Map*)features[i]->data;
      break;
    }
  }

  if (!map) {
    fprintf(stderr, "Host does not support urid:map.\n");
    return NULL;
  }

  ElDuderino* self = (ElDuderino*)malloc(sizeof(ElDuderino));

  self->map = map;
  self->uris.midi_MidiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);
  self->sample_rate = rate;
  
  return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
  ElDuderino* self = (ElDuderino*)instance;

  switch ((PortIndex)port) {
  case PORT_MIDI_IN:
    self->control = (const LV2_Atom_Sequence*)data;
    break;
  case PORT_GAIN:
    self->gain = (const float*)data;
    break;
  case PORT_PANNING:
    self->panning = (const float*)data;
    break;
  case PORT_ATTACK_LEVEL:
    self->attack_level = (const float*)data;
    break;
  case PORT_ATTACK_TIME:
    self->attack_time = (const float*)data;
    break;
  case PORT_SUSTAIN_LEVEL:
    self->sustain_level = (const float*)data;
    break;
  case PORT_DECAY_TIME:
    self->decay_time = (const float*)data;
    break;
  case PORT_RELEASE_TIME:
    self->release_time = (const float*)data;
    break;
  case PORT_AUDIO_OUT_LEFT:
    self->out_left  = (float*)data;
    break;
  case PORT_AUDIO_OUT_RIGHT:
    self->out_right = (float*)data;
    break;
  }
}

static void
activate(LV2_Handle instance)
{
  ElDuderino* self = (ElDuderino*)instance;

  for (int i_voice = 0; i_voice < N_VOICES; i_voice++) {
    self->voices[i_voice] = (Voice*)malloc(sizeof(Voice));
    Voice* voice = self->voices[i_voice];
    voice->velocity = 0;
    voice->key = 0;
    voice->phase = 0;
  }
}

static void
recalculate_params(ElDuderino* self) {
  if (self->attack_time != NULL) {
    self->attack_duration = self->sample_rate * (*self->attack_time) / 1000;
  }
  if (self->decay_time != NULL) {
    self->decay_duration = self->sample_rate * (*self->decay_time) / 1000;
  }
  if (self->release_time != NULL) {
    self->release_duration = self->sample_rate * (*self->release_time) / 1000;
  }
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
  ElDuderino* self = (ElDuderino*)instance;

  recalculate_params(self);

  uint32_t samples_done = 0;

  LV2_ATOM_SEQUENCE_FOREACH(self->control, ev) {
    if (ev->body.type == self->uris.midi_MidiEvent) {
      const uint8_t* const msg = (const uint8_t*)(ev + 1);

      switch (lv2_midi_message_type(msg)) {
        case LV2_MIDI_MSG_NOTE_ON:
          render_samples(samples_done, ev->time.frames, self);
          samples_done = ev->time.frames;

          note_on(self, msg[1], msg[2]);

          break;
        case LV2_MIDI_MSG_NOTE_OFF:
          render_samples(samples_done, ev->time.frames, self);
          samples_done = ev->time.frames;

          note_off(self, msg[1]);

          break;
        default: break;
      }
    }
  }

  render_samples(samples_done, n_samples, self);
}

static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
  free(instance);
}

const void*
extension_data(const char* uri)
{
  return NULL;
}

static const LV2_Descriptor descriptor = {
  ELDUDERINO_URI,
  instantiate,
  connect_port,
  activate,
  run,
  deactivate,
  cleanup,
  extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
  switch (index) {
  case 0:
    return &descriptor;
  default:
    return NULL;
  }
}
