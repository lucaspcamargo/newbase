#pragma once

#include <cstdbool>

#ifdef __cplusplus
extern "C" {
#endif

void _nb_xdg_data_dirname_search(const char* dirname);
bool _nb_xdg_data_dir_found();
const char* _nb_xdg_data_dirname_get();


#ifdef __cplusplus
}
#endif