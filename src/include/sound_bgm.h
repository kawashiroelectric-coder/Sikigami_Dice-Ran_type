/**
 * @file sound_bgm.h
 * @brief BGM配列の extern 宣言（BEACH_BGM.c / BONFIRE_BGM.c / RAIN_BGM.c で定義）
 */

#ifndef SOUND_BGM_H
#define SOUND_BGM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const int16_t BEACH_BGM[];
extern const int16_t BONFIRE_BGM[];
extern const int16_t RAIN_BGM[];

#define BEACH_BGM_LEN   123471U
#define BONFIRE_BGM_LEN 96287U
#define RAIN_BGM_LEN    50646U

#ifdef __cplusplus
}
#endif

#endif /* SOUND_BGM_H */
