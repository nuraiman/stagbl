#include "ctx.h"
#include "dump.h"
#include "system.h"
#include "system2.h"
#include "stagbl.h"
#include <stdio.h>
#include <petsc.h> // TODO REMOVE THIS INCLUDE
#include <mpi.h>
/* Note: This example application still uses PETSc for some tasks, but it is work in progress to properly hide everything behind the StagBL interface.. */

/* Helper functions */
static PetscErrorCode PopulateCoefficientData(Ctx);

/* Coefficient/forcing Functions */

/* Sinker */
static StagBLReal getRho_sinker(void *ptr,StagBLReal x, StagBLReal y) {
  Ctx ctx = (Ctx) ptr;
  const StagBLReal d = ctx->xmax-ctx->xmin;
  const StagBLReal xx = x/d - 0.5;
  const StagBLReal yy = y/d - 0.5;
  return (xx*xx + yy*yy) > 0.3*0.3 ? ctx->rho1 : ctx->rho2;
}
static StagBLReal getEta_sinker(void *ptr,StagBLReal x, StagBLReal y) {
  Ctx ctx = (Ctx) ptr;
  const StagBLReal d = ctx->xmax-ctx->xmin;
  const StagBLReal xx = x/d - 0.5;
  const StagBLReal yy = y/d - 0.5;
  return (xx*xx + yy*yy) > 0.3*0.3 ? ctx->eta1 : ctx->eta2;
}
/* Vertical layers */
StagBLReal getRho_gerya72(void *ptr,StagBLReal x,StagBLReal y)
{
  Ctx ctx = (Ctx) ptr;
  if (x + 0.0*y < (ctx->xmax-ctx->xmin)/2.0) {
    return ctx->rho1;
  } else {
    return ctx->rho2;
  }
}
StagBLReal getEta_gerya72(void *ptr,StagBLReal x,StagBLReal y)
{
  Ctx ctx = (Ctx) ptr;
  if (x  + 0.0*y < (ctx->xmax-ctx->xmin)/2.0) {
    return ctx->eta1;
  } else {
    return ctx->eta2;
  }
}

int main(int argc, char** argv)
{
  int                rank,size;
  StagBLGrid         grid;
  StagBLArray        x,b;
  StagBLOperator     A;
  StagBLLinearSolver solver;
  MPI_Comm           comm;

  Ctx                ctx;

  PetscErrorCode ierr;
  DM             *pdm;
  Vec            *pvecx,*pvecb;
  Vec            vecx,vecb;
  Mat            *pmatA;
  Mat            matA;
  KSP            *pksp;
  KSP            ksp;
  int            system;
  int            structure;

  /* Initialize MPI and print a message */
  MPI_Init(&argc,&argv);
  comm = MPI_COMM_WORLD;
  MPI_Comm_size(comm,&size);
  MPI_Comm_rank(comm,&rank);
  if (rank == 0) {
    printf("=== StagBLDemo2d ===\n");
    printf("%d ranks\n",size);
  }
  MPI_Barrier(comm);

  /* Initialize StagBL (which will initialize PETSc if needbe) */
  StagBLInitialize(argc,argv,comm);

  /* Accept argument for system type */
  system = 1;
  ierr = PetscOptionsGetInt(NULL,NULL,"-system",&system,NULL);CHKERRQ(ierr);

  /* Accept argument for coefficient structure */
  structure = 1;
  ierr = PetscOptionsGetInt(NULL,NULL,"-structure",&structure,NULL);CHKERRQ(ierr);

  /* Populate application context */
  ierr = PetscMalloc1(1,&ctx);CHKERRQ(ierr);
  ctx->comm = PETSC_COMM_WORLD;
  ctx->xmin = 0.0;
  ctx->xmax = 1e6;
  ctx->ymin = 0.0;
  ctx->ymax = 1.5e6;
  ctx->rho1 = 3200;
  ctx->rho2 = 3300;
  ctx->eta1 = 1e20;
  ctx->eta2 = 1e22;
  ctx->gy   = 10.0;

  switch (structure) {
    case 1:
      ctx->getEta = getEta_gerya72;
      ctx->getRho = getRho_gerya72;
      break;
    case 2:
      ctx->getEta = getEta_sinker;
      ctx->getRho = getRho_sinker;
      break;
    default: SETERRQ1(comm,PETSC_ERR_ARG_OUTOFRANGE,"Unsupported viscosity structure %d",structure);
  }

  /* Create a Grid */
  StagBLGridCreate(&grid);
  StagBLGridPETScGetDMPointer(grid,&pdm);
  ierr = DMStagCreate2d(
      comm,
      DM_BOUNDARY_NONE,DM_BOUNDARY_NONE,
      30,20,                                   /* Global element counts */
      PETSC_DECIDE,PETSC_DECIDE,               /* Determine parallel decomposition automatically */
      0,1,1,                                   /* dof: 0 per vertex, 1 per edge, 1 per face/element */
      DMSTAG_STENCIL_BOX,
      1,                                       /* elementwise stencil width */
      NULL,NULL,
      pdm);
  ctx->dmStokes = *pdm;
  ierr = DMSetFromOptions(ctx->dmStokes);CHKERRQ(ierr);
  ierr = DMSetUp(ctx->dmStokes);CHKERRQ(ierr);
  ierr = DMStagSetUniformCoordinatesProduct(ctx->dmStokes,0.0,ctx->xmax,0.0,ctx->ymax,0.0,0.0);CHKERRQ(ierr);
  ierr = DMStagCreateCompatibleDMStag(ctx->dmStokes,2,0,1,0,&ctx->dmCoeff);CHKERRQ(ierr);
  ierr = DMSetUp(ctx->dmCoeff);CHKERRQ(ierr);
  ierr = DMStagSetUniformCoordinatesProduct(ctx->dmCoeff,0.0,ctx->xmax,0.0,ctx->ymax,0.0,0.0);CHKERRQ(ierr);

  /* Get scaling constants and node to pin, knowing grid dimensions */
  {
    StagBLInt N[2];
    StagBLReal hxAvgInv;
    ierr = DMStagGetGlobalSizes(ctx->dmStokes,&N[0],&N[1],NULL);CHKERRQ(ierr);
    ctx->hxCharacteristic = (ctx->xmax-ctx->xmin)/N[0];
    ctx->hyCharacteristic = (ctx->ymax-ctx->ymin)/N[1];
    ctx->etaCharacteristic = PetscMin(ctx->eta1,ctx->eta2);
    hxAvgInv = 2.0/(ctx->hxCharacteristic + ctx->hyCharacteristic);
    ctx->Kcont = ctx->etaCharacteristic*hxAvgInv;
    ctx->Kbound = ctx->etaCharacteristic*hxAvgInv*hxAvgInv;
    if (N[0] < 2) SETERRQ(comm,PETSC_ERR_SUP,"Not implemented for a single element in the x direction");
    ctx->pinx = 1; ctx->piny = 0;
  }

  /* Populate coefficient data */
  ierr = PopulateCoefficientData(ctx);CHKERRQ(ierr);

  /* Create a system */
  StagBLOperatorCreate(&A);
  StagBLArrayCreate(&x);
  StagBLArrayCreate(&b);

  StagBLArrayPETScGetVecPointer(x,&pvecx);
  ierr = DMCreateGlobalVector(ctx->dmStokes,pvecx);
  vecx = *pvecx;
  ierr = PetscObjectSetName((PetscObject)vecx,"solution");CHKERRQ(ierr);

  StagBLArrayPETScGetVecPointer(b,&pvecb);

  StagBLOperatorPETScGetMatPointer(A,&pmatA);
  if (system == 1) {
    ierr = CreateSystem(ctx,pmatA,pvecb);CHKERRQ(ierr);
  } else if (system == 2) {
    ierr = CreateSystem2(ctx,pmatA,pvecb);CHKERRQ(ierr);
  } else SETERRQ1(ctx->comm,PETSC_ERR_SUP,"Unsupported system type %D",system);
  matA = *pmatA;
  vecb = *pvecb;

  /* Solve the system (you will likely want to specify a solver from the command line) */
  StagBLLinearSolverCreate(&solver);
  StagBLLinearSolverPETScGetKSPPointer(solver,&pksp);

  ierr = VecDuplicate(vecb,pvecx);CHKERRQ(ierr);
  ierr = KSPCreate(ctx->comm,pksp);CHKERRQ(ierr);
  ksp = *pksp;
  ierr = KSPSetOperators(ksp,matA,matA);CHKERRQ(ierr);
  ierr = KSPSetFromOptions(ksp);CHKERRQ(ierr);
  ierr = KSPSolve(ksp,vecb,vecx);CHKERRQ(ierr);
  {
    KSPConvergedReason reason;
    ierr = KSPGetConvergedReason(ksp,&reason);CHKERRQ(ierr);
    if (reason < 0) SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_CONV_FAILED,"Linear solve failed");CHKERRQ(ierr);
  }

  /* Dump solution by converting to DMDAs and dumping */
  ierr = DumpSolution(ctx,vecx);CHKERRQ(ierr);

  /* Free data */
  StagBLArrayDestroy(&x);
  StagBLArrayDestroy(&b);
  StagBLOperatorDestroy(&A);
  StagBLLinearSolverDestroy(&solver);
  StagBLGridDestroy(&grid);

  StagBLFinalize();

  return 0;
}

/* Here, we demonstrate getting coordinates from a vector by using DMStagStencil.
This would usually be done with direct array access, though. */
static PetscErrorCode PopulateCoefficientData(Ctx ctx)
{
  PetscErrorCode ierr;
  StagBLInt      N[2],nDummy[2];
  StagBLInt      ex,ey,startx,starty,nx,ny,ietaCorner,ietaElement,irho,iprev,icenter;
  Vec            coeffLocal;
  StagBLReal     **cArrX,**cArrY;
  StagBLReal     ***coeffArr;

  PetscFunctionBeginUser;
  ierr = DMGetLocalVector(ctx->dmCoeff,&coeffLocal);CHKERRQ(ierr);
  ierr = DMStagGetCorners(ctx->dmCoeff,&startx,&starty,NULL,&nx,&ny,NULL,&nDummy[0],&nDummy[1],NULL);CHKERRQ(ierr);
  ierr = DMStagGetGlobalSizes(ctx->dmCoeff,&N[0],&N[1],NULL);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(ctx->dmCoeff,DMSTAG_DOWN_LEFT,0,&ietaCorner);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(ctx->dmCoeff,DMSTAG_ELEMENT,0,&ietaElement);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(ctx->dmCoeff,DMSTAG_DOWN_LEFT,1,&irho);CHKERRQ(ierr);

  ierr = DMStagGet1dCoordinateArraysDOFRead(ctx->dmCoeff,&cArrX,&cArrY,NULL);CHKERRQ(ierr);
  ierr = DMStagGet1dCoordinateLocationSlot(ctx->dmCoeff,DMSTAG_ELEMENT,&icenter);CHKERRQ(ierr);
  ierr = DMStagGet1dCoordinateLocationSlot(ctx->dmCoeff,DMSTAG_LEFT,&iprev);CHKERRQ(ierr);

  ierr = DMStagVecGetArrayDOF(ctx->dmCoeff,coeffLocal,&coeffArr);CHKERRQ(ierr);

  for (ey = starty; ey<starty+ny+nDummy[1]; ++ey) {
    for (ex = startx; ex<startx+nx+nDummy[0]; ++ex) {
      coeffArr[ey][ex][ietaElement] = ctx->getEta(ctx,cArrX[ex][icenter],cArrY[ey][icenter]); // Note dummy value filled here, needlessly
      coeffArr[ey][ex][ietaCorner]  = ctx->getEta(ctx,cArrX[ex][iprev],cArrY[ey][iprev]);
      coeffArr[ey][ex][irho]        = ctx->getRho(ctx,cArrX[ex][iprev],cArrY[ey][iprev]);
    }
  }
  ierr = DMStagRestore1dCoordinateArraysDOFRead(ctx->dmCoeff,&cArrX,&cArrY,NULL);CHKERRQ(ierr);
  ierr = DMStagVecRestoreArrayDOF(ctx->dmCoeff,coeffLocal,&coeffArr);CHKERRQ(ierr);
  ierr = DMCreateGlobalVector(ctx->dmCoeff,&ctx->coeff);CHKERRQ(ierr);
  ierr = DMLocalToGlobalBegin(ctx->dmCoeff,coeffLocal,INSERT_VALUES,ctx->coeff);CHKERRQ(ierr);
  ierr = DMLocalToGlobalEnd(ctx->dmCoeff,coeffLocal,INSERT_VALUES,ctx->coeff);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(ctx->dmCoeff,&coeffLocal);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

