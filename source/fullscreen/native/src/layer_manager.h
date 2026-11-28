#ifndef ID6_LIBRARY_LAYER_MANAGER_H
#define ID6_LIBRARY_LAYER_MANAGER_H

/* LayerManager creates a surface from the decimal QNX Screen window ID. */
#define LIBRARY_SURFACE 1290u
#define LIBRARY_SCREEN_ID "1290"
#define FACTORY_HMI_SURFACE 256u

enum layer_result { layer_error = -1, layer_waiting = 0, layer_ready = 1 };

bool layer_manager_available(void);
bool layer_manager_size(int *width, int *height);
bool layer_manager_surface_bound(void);
enum layer_result layer_manager_show(int width, int height);
void layer_manager_hide(void);

#endif
