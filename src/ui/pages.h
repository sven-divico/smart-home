#pragma once
#include "canvas1.h"
#include "ui_model.h"

// Page renderers — pure drawing into a Canvas1, no hardware calls. The same
// functions run on the panel (canvas over s_frame) and on the host (-> PGM).
void renderMain(Canvas1 &c, const UiModel &m);
void renderDetail(Canvas1 &c, const UiModel &m);
void renderActuators(Canvas1 &c, const UiModel &m, int focus);  // focus = highlighted row
