#if !defined(STAGBL_H_)
#define STAGBL_H_

#include <petsc.h>

PetscBool StagBLCheckType(const char*,const char*);
PetscErrorCode StagBLEnforceType(const char*,const char*);
PetscErrorCode StagBLInitialize(int,char**,const char*,MPI_Comm);
PetscErrorCode StagBLFinalize();

#define StagBLError(comm,str) SETERRQ(comm,PETSC_ERR_LIB,"StagBL Error: "str);
#define StagBLError1(comm,str,arg) SETERRQ1(comm,PETSC_ERR_LIB,"StagBL Error: "str,arg);
#define StagBLError2(comm,str,arg1,arg2) SETERRQ2(comm,PETSC_ERR_LIB,"StagBL Error: "str,arg1,arg2);

#define STAGBL_UNUSED(x) (void) x

/* StagBLGrid Data */
struct data_StagBLGrid;
typedef struct data_StagBLGrid *StagBLGrid;
typedef const char * StagBLGridType;

/* StagBLArray Data */
struct data_StagBLArray;
typedef struct data_StagBLArray *StagBLArray;
typedef const char * StagBLArrayType;

/* StagBLSystem Data */
struct data_StagBLSystem;
typedef struct data_StagBLSystem *StagBLSystem;
typedef const char * StagBLSystemType;

/* StagBLGrid impls */
#define STAGBLGRIDPETSC "petsc"

/* StagBLGrid Functions */
PetscErrorCode StagBLGridCreate(StagBLGrid*,StagBLGridType);
PetscErrorCode StagBLGridCreateCompatibleStagBLGrid(StagBLGrid,PetscInt,PetscInt,PetscInt,PetscInt,StagBLGrid*);
PetscErrorCode StagBLGridCreateStagBLArray(StagBLGrid,StagBLArray*);
PetscErrorCode StagBLGridCreateStagBLSystem(StagBLGrid,StagBLSystem*);
PetscErrorCode StagBLGridDestroy(StagBLGrid*);
PetscErrorCode StagBLGridSetArrayType(StagBLGrid,StagBLArrayType);
PetscErrorCode StagBLGridSetSystemType(StagBLGrid,StagBLSystemType);

PetscErrorCode StagBLGridPETScGetDM(StagBLGrid,DM*);
PetscErrorCode StagBLGridPETScGetDMPointer(StagBLGrid,DM**);

/* StagBLArray Functions */
PetscErrorCode StagBLArrayCreate(StagBLGrid,StagBLArray*,StagBLArrayType);
PetscErrorCode StagBLArrayDestroy(StagBLArray*);
PetscErrorCode StagBLArrayGetLocalValuesStencil(StagBLArray,PetscInt,const DMStagStencil*,PetscScalar*);
PetscErrorCode StagBLArrayGetStagBLGrid(StagBLArray,StagBLGrid*);
PetscErrorCode StagBLArrayGetType(StagBLArray,StagBLArrayType*);
PetscErrorCode StagBLArraySetGlobalCurrent(StagBLArray,PetscBool);
PetscErrorCode StagBLArraySetLocalCurrent(StagBLArray,PetscBool);
PetscErrorCode StagBLArrayGlobalToLocal(StagBLArray);
PetscErrorCode StagBLArrayLocalToGlobal(StagBLArray);
PetscErrorCode StagBLArraySetLocalConstant(StagBLArray,PetscScalar);
PetscErrorCode StagBLArraySetLocalValuesStencil(StagBLArray,PetscInt,const DMStagStencil*,const PetscScalar*);
PetscErrorCode StagBLArrayPrint(StagBLArray);

/* StagBLArray impls */
#define STAGBLARRAYPETSC "petsc"
PetscErrorCode StagBLArrayCreate_PETSc(StagBLArray);
PetscErrorCode StagBLArrayPETScGetLocalVec(StagBLArray,Vec*);
PetscErrorCode StagBLArrayPETScGetGlobalVec(StagBLArray,Vec*);
PetscErrorCode StagBLArrayPETScGetLocalVecPointer(StagBLArray,Vec**);
PetscErrorCode StagBLArrayPETScGetGlobalVecPointer(StagBLArray,Vec**);

#define STAGBLARRAYSIMPLE "simple"
PetscErrorCode StagBLArrayCreate_Simple(StagBLArray);
PetscErrorCode StagBLArraySimpleGetGlobalRaw(StagBLArray,PetscScalar**);
PetscErrorCode StagBLArraySimpleGetLocalRaw(StagBLArray,PetscScalar**);

/* StagBLSystem Functions */
PetscErrorCode StagBLSystemCreate(StagBLGrid,StagBLSystem*,StagBLSystemType);
PetscErrorCode StagBLSystemDestroy(StagBLSystem*);
PetscErrorCode StagBLSystemGetGrid(StagBLSystem,StagBLGrid*);
PetscErrorCode StagBLSystemSolve(StagBLSystem,StagBLArray);

PetscErrorCode StagBLSystemOperatorSetValuesStencil(StagBLSystem,PetscInt,const DMStagStencil*,PetscInt,const DMStagStencil*,const PetscScalar*);
PetscErrorCode StagBLSystemRHSSetConstant(StagBLSystem,PetscScalar);
PetscErrorCode StagBLSystemRHSSetValuesStencil(StagBLSystem,PetscInt,const DMStagStencil*,const PetscScalar*);

/* StagBLSystem impls */
#define STAGBLSYSTEMPETSC "petsc"
PetscErrorCode StagBLSystemCreate_PETSc(StagBLSystem);
PetscErrorCode StagBLSystemPETScGetMat(StagBLSystem,Mat*);
PetscErrorCode StagBLSystemPETScGetMatPointer(StagBLSystem,Mat**);
PetscErrorCode StagBLSystemPETScGetVec(StagBLSystem,Vec*);
PetscErrorCode StagBLSystemPETScGetVecPointer(StagBLSystem,Vec**);
PetscErrorCode StagBLSystemPETScGetResidualFunction(StagBLSystem,PetscErrorCode (**f)(SNES,Vec,Vec,void*));
PetscErrorCode StagBLSystemPETScGetJacobianFunction(StagBLSystem,PetscErrorCode (**f)(SNES,Vec,Mat,Mat,void*));

#define STAGBLSYSTEMSIMPLE "simple"
PetscErrorCode StagBLSystemCreate_Simple(StagBLSystem);

/* Stokes */
struct data_StagBLStokesParameters {
  StagBLGrid  stokes_grid,temperature_grid;
  StagBLArray coefficient_array,temperature_array;
  PetscBool   uniform_grid, boussinesq_forcing;
  PetscReal   xmin,xmax,ymin,ymax,zmin,zmax;
  PetscReal   gy,eta_characteristic,alpha;
};
typedef struct data_StagBLStokesParameters *StagBLStokesParameters;

PetscErrorCode StagBLStokesParametersCreate(StagBLStokesParameters*);
PetscErrorCode StagBLStokesParametersDestroy(StagBLStokesParameters*);

PetscErrorCode StagBLGridCreateStokes2DBox(MPI_Comm,PetscInt,PetscInt,PetscScalar,PetscScalar,PetscScalar,PetscScalar,StagBLGrid*);
PetscErrorCode StagBLGridCreateStokes3DBox(MPI_Comm,PetscInt,PetscInt,PetscInt,PetscScalar,PetscScalar,PetscScalar,PetscScalar,PetscScalar,PetscScalar,StagBLGrid*);
PetscErrorCode StagBLCreateStokesSystem(StagBLStokesParameters,StagBLSystem*);

PetscErrorCode StagBLDumpStokes(StagBLStokesParameters,StagBLArray,PetscInt);

#endif
