#ifndef scriptable_encoder_h
#define scriptable_encoder_h

#include "scriptable.h"
#include "../plugins/converter/converter.h"

#ifdef __cplusplus
extern "C" {
#endif

scriptableItem_t *
scriptableEncoderRoot (void);

void
scriptableEncoderLoadPresets (void);

void
scriptableEncoderPresetToConverterEncoderPreset (scriptableItem_t *item, ddb_encoder_preset_t *encoder_preset);

#ifdef __cplusplus
}
#endif

#endif /* scriptable_encoder_h */
