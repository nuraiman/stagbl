#include "coeff.h"

/* Coefficient/forcing Functions */

/* Constant */
static StagBLReal getRho_constant(void *ptr,StagBLReal x, StagBLReal y) {
  Ctx ctx = (Ctx) ptr;
  return ctx->rho1;
}

static StagBLReal getEta_constant(void *ptr,StagBLReal x, StagBLReal y) {
  Ctx ctx = (Ctx) ptr;
  return ctx->rho2;
}

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
static StagBLReal getRho_gerya72(void *ptr,StagBLReal x,StagBLReal y)
{
  Ctx ctx = (Ctx) ptr;
  if (x + 0.0*y < (ctx->xmax-ctx->xmin)/2.0) {
    return ctx->rho1;
  } else {
    return ctx->rho2;
  }
}

static StagBLReal getEta_gerya72(void *ptr,StagBLReal x,StagBLReal y)
{
  Ctx ctx = (Ctx) ptr;
  if (x  + 0.0*y < (ctx->xmax-ctx->xmin)/2.0) {
    return ctx->eta1;
  } else {
    return ctx->eta2;
  }
}

PetscErrorCode PopulateCoefficientData(Ctx ctx,const char* mode)
{
  PetscErrorCode ierr;
  PetscInt       N[2];
  PetscInt       ex,ey,startx,starty,nx,ny,ietaCorner,ietaElement,irho,iprev,icenter;
  DM             dmCoeff;
  Vec            *pcoeffLocal;
  Vec            coeffLocal;
  PetscReal      **cArrX,**cArrY;
  PetscReal      ***coeffArr;
  PetscBool      flg;

  PetscFunctionBeginUser;

  flg = PETSC_FALSE;
  ierr = PetscStrcmp(mode,"gerya72",&flg);CHKERRQ(ierr);
  if (flg) {
    ctx->getEta = getEta_gerya72;
    ctx->getRho = getRho_gerya72;
  }
  if (!flg) {
    ierr = PetscStrcmp(mode,"sinker",&flg);CHKERRQ(ierr);
    if (flg) {
      ctx->getEta = getEta_sinker;
      ctx->getRho = getRho_sinker;
    }
  }
  if (!flg) {
    ierr = PetscStrcmp(mode,"blankenbach_1",&flg);CHKERRQ(ierr);
    if (flg) {
      ctx->getEta = getEta_constant;
      ctx->getRho = getRho_constant;
    }
  }
  if (!flg) {
    SETERRQ1(ctx->comm,PETSC_ERR_ARG_OUTOFRANGE,"Unrecognized mode %s",mode);
  }

  ierr = StagBLGridCreateStagBLArray(ctx->coeffGrid,&ctx->coeffArray);CHKERRQ(ierr);

  /* Escape Hatch */
  ierr = StagBLGridPETScGetDM(ctx->coeffGrid,&dmCoeff);CHKERRQ(ierr);
  ierr = StagBLArrayPETScGetLocalVecPointer(ctx->coeffArray,&pcoeffLocal);CHKERRQ(ierr);

  ierr = DMCreateLocalVector(dmCoeff,pcoeffLocal);CHKERRQ(ierr);
  coeffLocal = *pcoeffLocal;
  ierr = DMStagGetGhostCorners(dmCoeff,&startx,&starty,NULL,&nx,&ny,NULL);CHKERRQ(ierr); /* Iterate over all local elements */
  ierr = DMStagGetGlobalSizes(dmCoeff,&N[0],&N[1],NULL);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dmCoeff,DMSTAG_DOWN_LEFT,0,&ietaCorner);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dmCoeff,DMSTAG_ELEMENT,0,&ietaElement);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dmCoeff,DMSTAG_DOWN_LEFT,1,&irho);CHKERRQ(ierr);

  ierr = DMStagGet1dCoordinateArraysDOFRead(dmCoeff,&cArrX,&cArrY,NULL);CHKERRQ(ierr);
  ierr = DMStagGet1dCoordinateLocationSlot(dmCoeff,DMSTAG_ELEMENT,&icenter);CHKERRQ(ierr);
  ierr = DMStagGet1dCoordinateLocationSlot(dmCoeff,DMSTAG_LEFT,&iprev);CHKERRQ(ierr);

  ierr = DMStagVecGetArrayDOF(dmCoeff,coeffLocal,&coeffArr);CHKERRQ(ierr);

  for (ey = starty; ey<starty+ny; ++ey) {
    for (ex = startx; ex<startx+nx; ++ex) {
      coeffArr[ey][ex][ietaElement] = ctx->getEta(ctx,cArrX[ex][icenter],cArrY[ey][icenter]);
      coeffArr[ey][ex][ietaCorner]  = ctx->getEta(ctx,cArrX[ex][iprev],cArrY[ey][iprev]);
      coeffArr[ey][ex][irho]        = ctx->getRho(ctx,cArrX[ex][iprev],cArrY[ey][iprev]);
    }
  }
  ierr = DMStagRestore1dCoordinateArraysDOFRead(dmCoeff,&cArrX,&cArrY,NULL);CHKERRQ(ierr);
  ierr = DMStagVecRestoreArrayDOF(dmCoeff,coeffLocal,&coeffArr);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

