#ifndef SOUND_CLASSIFIER_H
#define SOUND_CLASSIFIER_H
#include <stdint.h>


typedef struct 
{
    uint16_t latitude; // 0-90
    uint16_t longitude; // 0-180
    uint8_t class;// 0-255 
}classifier_data;

void read_gpio_sound_classifier(classifier_data *data);

#endif