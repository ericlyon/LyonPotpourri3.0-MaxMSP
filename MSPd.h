#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "ext.h"
#include "z_dsp.h"
#include "ext_obex.h"
#include "buffer.h"
#define t_floatarg double

#define LYONPOTPOURRI_MSG "-{LyonPotpourri 3.0}-"
#define potpourri_announce(objname)  post("%s\t ( %s )",LYONPOTPOURRI_MSG,objname)
