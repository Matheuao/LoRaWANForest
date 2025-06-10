#include "sound_classifier.h"


void read_gpio_sound_classifier(classifier_data *data){

    //rotina para ler valores do gpio

    data->latitude = 90;
    data->longitude = 180;
    data->latitude_direction ="w";
    data->longitude_direction = "s";

    data->class = 255;
}



