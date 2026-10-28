#ifndef CANDIDATE_WIDGETS_H
#define CANDIDATE_WIDGETS_H

#include <QString>

class main_window;
class image_viewer_window;
class waterfall_scene;

void review_library(main_window& window, const QString& directory);
void review_preview(image_viewer_window& window, waterfall_scene& scene);

#endif
