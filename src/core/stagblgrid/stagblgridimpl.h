#if !defined(STAGBLGRIDIMPL_H_)
#define STAGBLGRIDIMPL_H_

#include "stagbl.h"

struct _p_StagBLGridOps {
  void (*create)(StagBLGrid);
  void (*destroy)(StagBLGrid);
};

typedef struct _p_StagBLGridOps *StagBLGridOps;

struct _p_StagBLGrid
{
  StagBLGridOps ops;
  const char    *type;
  void          *data;
};

#endif