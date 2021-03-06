#if !defined(STAGBLARRAYIMPL_H_)
#define STAGBLARRAYIMPL_H_

#include "stagbl.h"

struct data_StagBLArrayOps {
  PetscErrorCode (*create)(StagBLArray);
  PetscErrorCode (*destroy)(StagBLArray);
  PetscErrorCode (*getlocalvaluesstencil)(StagBLArray,PetscInt,const DMStagStencil*,PetscScalar*);
  PetscErrorCode (*globaltolocal)(StagBLArray);
  PetscErrorCode (*localtoglobal)(StagBLArray);
  PetscErrorCode (*print)(StagBLArray);
  PetscErrorCode (*setlocalconstant)(StagBLArray,PetscScalar);
  PetscErrorCode (*setlocalvaluesstencil)(StagBLArray,PetscInt,const DMStagStencil*,const PetscScalar*);
};
typedef struct data_StagBLArrayOps *StagBLArrayOps;

struct data_StagBLArray
{
  StagBLArrayOps   ops;
  StagBLGrid       grid;
  StagBLArrayType  type;
  PetscBool        current_local, current_global;
  void             *data;
};

#endif
