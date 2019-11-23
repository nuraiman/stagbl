#if !defined(STAGBLSOLVERIMPL_H_)
#define STAGBLSOLVERIMPL_H_

#include "stagbl.h"

struct _p_StagBLSolverOps {
  PetscErrorCode (*create)(StagBLSolver);
  PetscErrorCode (*destroy)(StagBLSolver);
  PetscErrorCode (*solve)(StagBLSolver,StagBLArray);
};

typedef struct _p_StagBLSolverOps *StagBLSolverOps;

struct _p_StagBLSolver
{
  StagBLSolverOps ops;
  const char    *type;
  void          *data;
  StagBLSystem  system;
};

#endif
