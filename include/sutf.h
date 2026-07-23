#ifndef SUTF_MASTER_H
#define SUTF_MASTER_H

#include "sucs_types.h"
#include "sucs_mode.h"
#include "sutf8.h"
#include "sutf16.h"

#if defined(__has_include)
  #if __has_include("sutf4.h")
    #include "sutf4.h"
  #endif
  #if __has_include("sutf2.h")
    #include "sutf2.h"
  #endif
  #if __has_include("extsutf_fixed.h")
    #include "extsutf_fixed.h"
  #endif
  #if __has_include("vsutf.h")
    #include "vsutf.h"
  #endif
  #if __has_include("esutf.h")
    #include "esutf.h"
  #endif
#endif

#endif /* SUTF_MASTER_H */
