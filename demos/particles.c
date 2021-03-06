#include "particles.h"

PetscErrorCode CreateParticleSystem(Ctx ctx)
{
  PetscErrorCode ierr;
  DM dmStokes;
  PetscInt particlesPerElementPerDim,n[2],nel;

  PetscFunctionBeginUser;
  particlesPerElementPerDim = 3;
  ierr = PetscOptionsGetInt(NULL,NULL,"-p",&particlesPerElementPerDim,NULL);CHKERRQ(ierr);
  ierr = DMCreate(PETSC_COMM_WORLD,&ctx->dm_particles);CHKERRQ(ierr);
  ierr = DMSetType(ctx->dm_particles,DMSWARM);CHKERRQ(ierr);
  ierr = DMSetDimension(ctx->dm_particles,2);CHKERRQ(ierr);
  ierr = DMSwarmSetType(ctx->dm_particles,DMSWARM_PIC);CHKERRQ(ierr);
  ierr = StagBLGridPETScGetDM(ctx->stokes_grid,&dmStokes);CHKERRQ(ierr);
  ierr = DMStagGetLocalSizes(dmStokes,&n[0],&n[1],NULL);CHKERRQ(ierr);
  nel = n[0]*n[1];
  ierr = DMSwarmSetCellDM(ctx->dm_particles,dmStokes);CHKERRQ(ierr);
  /* Note: quantities could be carried on the particles, e.g.
  ierr = DMSwarmRegisterPetscDatatypeField(ctx->dm_particles,"rho",1,PETSC_REAL);CHKERRQ(ierr);
  ierr = DMSwarmRegisterPetscDatatypeField(ctx->dm_particles,"eta",1,PETSC_REAL);CHKERRQ(ierr);
  */
  ierr = DMSwarmRegisterPetscDatatypeField(ctx->dm_particles,"Temperature",1,PETSC_REAL);CHKERRQ(ierr);
  ierr = DMSwarmFinalizeFieldRegister(ctx->dm_particles);CHKERRQ(ierr);
  ierr = DMSwarmSetLocalSizes(ctx->dm_particles,nel*particlesPerElementPerDim,100);CHKERRQ(ierr);

  /* Define initial point positions */
  ierr = DMSwarmInsertPointsUsingCellDM(ctx->dm_particles,DMSWARMPIC_LAYOUT_REGULAR,particlesPerElementPerDim);CHKERRQ(ierr);

  /* Set properties for particles.  Each particle has a viscosity and density
     (in some applications one would use material ids ) */
  {
    PetscReal   *array_x,jiggle,dh;
    //PetscReal   *array_e,*array_r;
    PetscInt    npoints,p,N[2];
    PetscRandom r;
    PetscMPIInt rank;

    /* Displace particles randomly */
    jiggle = 0.1;
    ierr = PetscOptionsGetReal(NULL,NULL,"-jiggle",&jiggle,NULL);CHKERRQ(ierr);
    ierr = DMStagGetGlobalSizes(dmStokes,&N[0],&N[1],NULL);CHKERRQ(ierr);
    dh = PetscMin((ctx->xmax-ctx->xmin)/N[0],(ctx->ymax-ctx->ymin)/N[1]);
    if (jiggle > 0.0) {
      ierr = PetscRandomCreate(PETSC_COMM_SELF,&r);CHKERRQ(ierr);
      ierr = PetscRandomSetInterval(r,-jiggle*dh,jiggle*dh);CHKERRQ(ierr);
      ierr = MPI_Comm_rank(PetscObjectComm((PetscObject)dmStokes),&rank);CHKERRQ(ierr);
      ierr = PetscRandomSetSeed(r,(unsigned long)rank);CHKERRQ(ierr);
      ierr = PetscRandomSeed(r);CHKERRQ(ierr);
    }

    /* Compute coefficient values on particles */
    ierr = DMSwarmGetField(ctx->dm_particles,DMSwarmPICField_coor,NULL,NULL,(void**)&array_x);CHKERRQ(ierr);
    /* Note: quanitites could be stored on the particles, e.g.
    ierr = DMSwarmGetField(ctx->dm_particles,"eta",               NULL,NULL,(void**)&array_e);CHKERRQ(ierr);
    ierr = DMSwarmGetField(ctx->dm_particles,"rho",               NULL,NULL,(void**)&array_r);CHKERRQ(ierr);
    */
    ierr = DMSwarmGetLocalSize(ctx->dm_particles,&npoints);CHKERRQ(ierr);
    for (p = 0; p < npoints; p++) {
      /* Note: quantities could be computed on the particles, e.g.:
      PetscReal x_p[2];

      x_p[0] = array_x[2*p + 0];
      x_p[1] = array_x[2*p + 1];
      array_e[p] = getEta(ctx,x_p[0],x_p[1]);
      array_r[p] = getRho(ctx,x_p[0],x_p[1]);
      */

      if (jiggle > 0.0) {
        PetscReal rr[2];
        ierr = PetscRandomGetValueReal(r,&rr[0]);CHKERRQ(ierr);
        ierr = PetscRandomGetValueReal(r,&rr[1]);CHKERRQ(ierr);
        array_x[2*p + 0] += rr[0];
        array_x[2*p + 1] += rr[1];
      }

    }
    /* Note: quanitites could be stored on the particles, e.g.
    ierr = DMSwarmRestoreField(ctx->dm_particles,"rho",NULL,NULL,(void**)&array_r);CHKERRQ(ierr);
    ierr = DMSwarmRestoreField(ctx->dm_particles,"eta",NULL,NULL,(void**)&array_e);CHKERRQ(ierr);
    */
    ierr = DMSwarmRestoreField(ctx->dm_particles,DMSwarmPICField_coor,NULL,NULL,(void**)&array_x);CHKERRQ(ierr);

    if (jiggle > 0.0) {
      ierr = PetscRandomDestroy(&r);CHKERRQ(ierr);
    }
  }

  /* Migrate - this is important since we may have "jiggled" particles
     into different cells, or off the local subdomain */
  ierr = DMSwarmMigrate(ctx->dm_particles,PETSC_TRUE);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}

PetscErrorCode InterpolateTemperatureToParticles(Ctx ctx)
{
  PetscErrorCode ierr;
  DM             dm_temperature,dm_mpoint;
  Vec            temp,tempLocal;
  PetscInt       p,e,npoints;
  PetscInt       *mpfield_cell;
  PetscReal      *array_temperature;
  PetscScalar    ***arr;
  const PetscScalar **arr_coordinates_x,**arr_coordinates_y;
  PetscInt       slot_temperature_downleft,slot_temperature_downright,slot_temperature_upleft,slot_temperature_upright;
  PetscInt       slot_coordinates_prev,slot_coordinates_next;
  PetscReal      *particle_coordinates;

  PetscFunctionBeginUser;
  dm_mpoint = ctx->dm_particles;
  ierr = StagBLGridPETScGetDM(ctx->temperature_grid,&dm_temperature);CHKERRQ(ierr);
  ierr = StagBLArrayPETScGetGlobalVec(ctx->temperature_array,&temp);CHKERRQ(ierr);
  ierr = DMGetLocalVector(dm_temperature,&tempLocal);CHKERRQ(ierr);
  ierr = DMGlobalToLocal(dm_temperature,temp,INSERT_VALUES,tempLocal);CHKERRQ(ierr);

  ierr = DMStagGetLocationSlot(dm_temperature,DMSTAG_DOWN_LEFT,0,&slot_temperature_downleft);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dm_temperature,DMSTAG_DOWN_RIGHT,0,&slot_temperature_downright);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dm_temperature,DMSTAG_UP_RIGHT,0,&slot_temperature_upright);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dm_temperature,DMSTAG_UP_LEFT,0,&slot_temperature_upleft);CHKERRQ(ierr);
  ierr = DMStagVecGetArrayRead(dm_temperature,tempLocal,&arr);CHKERRQ(ierr);
  ierr = DMStagGetProductCoordinateArraysRead(dm_temperature,&arr_coordinates_x,&arr_coordinates_y,NULL);CHKERRQ(ierr);
  ierr = DMStagGetProductCoordinateLocationSlot(dm_temperature,DMSTAG_LEFT,&slot_coordinates_prev);CHKERRQ(ierr);
  ierr = DMStagGetProductCoordinateLocationSlot(dm_temperature,DMSTAG_RIGHT,&slot_coordinates_next);CHKERRQ(ierr);
  ierr = DMSwarmGetField(dm_mpoint,DMSwarmPICField_cellid,NULL,NULL,(void**)&mpfield_cell);CHKERRQ(ierr);
  ierr = DMSwarmGetField(ctx->dm_particles,DMSwarmPICField_coor,NULL,NULL,(void**)&particle_coordinates);CHKERRQ(ierr);
  ierr = DMSwarmGetField(ctx->dm_particles,"Temperature",NULL,NULL,(void**)&array_temperature);CHKERRQ(ierr);
  ierr = DMSwarmGetLocalSize(dm_mpoint,&npoints);CHKERRQ(ierr);
  for (p=0; p<npoints; p++) {
    PetscInt    ind[2],ex,ey;
    PetscReal   px,py,x_left,x_right,y_down,y_up,x_local,y_local;

    e       = mpfield_cell[p];
    ierr = DMStagGetLocalElementGlobalIndices(dm_temperature,e,ind);CHKERRQ(ierr);
    ex = ind[0];
    ey = ind[1];

    /* Linearly interpolate corner values */
    px = particle_coordinates[2*p];
    py = particle_coordinates[2*p+1];
    y_down = arr_coordinates_y[ey][slot_coordinates_prev];
    y_up = arr_coordinates_y[ey][slot_coordinates_next];
    x_left = arr_coordinates_x[ex][slot_coordinates_prev];
    x_right = arr_coordinates_x[ex][slot_coordinates_next];
    x_local = (px - x_left)/(x_right - x_left);
    y_local = (py - y_down)/(y_up - y_down);
    array_temperature[p] =
            (1.0 - x_local) * (1.0 - y_local) * arr[ey][ex][slot_temperature_downleft]
          + x_local         * (1.0 - y_local) * arr[ey][ex][slot_temperature_downright]
          + (1.0 - x_local) * y_local         * arr[ey][ex][slot_temperature_upleft]
          + x_local         * y_local         * arr[ey][ex][slot_temperature_upright];
  }
  ierr = DMStagVecRestoreArrayRead(dm_temperature,tempLocal,&arr);CHKERRQ(ierr);
  ierr = DMStagRestoreProductCoordinateArraysRead(dm_temperature,&arr_coordinates_x,&arr_coordinates_y,NULL);CHKERRQ(ierr);
  ierr = DMSwarmRestoreField(ctx->dm_particles,DMSwarmPICField_coor,NULL,NULL,(void**)&particle_coordinates);CHKERRQ(ierr);
  ierr = DMSwarmRestoreField(ctx->dm_particles,"Temperature",NULL,NULL,(void**)&array_temperature);CHKERRQ(ierr);
  ierr = DMSwarmRestoreField(dm_mpoint,DMSwarmPICField_cellid,NULL,NULL,(void**)&mpfield_cell);CHKERRQ(ierr);

  ierr = DMRestoreLocalVector(dm_temperature,&tempLocal);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PETSC_STATIC_INLINE PetscReal RangeMod(PetscReal a,PetscReal xmin,PetscReal xmax) { PetscReal range = xmax-xmin; return xmin +PetscFmodReal(range+PetscFmodReal(a,range),range); }

PetscErrorCode MaterialPoint_AdvectRK1(Ctx ctx,Vec vp,PetscReal dt)
{
  PetscErrorCode    ierr;
  Vec               vp_l;
  const PetscScalar ***LA_vp;
  PetscInt          p,e,npoints;
  PetscInt          *mpfield_cell;
  PetscReal         *mpfield_coor;
  PetscScalar       **cArrX,**cArrY;
  PetscInt          iPrev,iNext,iCenter,iVxLeft,iVxRight,iVyDown,iVyUp,n[2];
  DMBoundaryType    boundaryTypes[2];
  PetscBool         periodicx;
  DM                dm_vp,dm_mpoint;

  PetscFunctionBeginUser;
  ierr =StagBLGridPETScGetDM(ctx->stokes_grid,&dm_vp);CHKERRQ(ierr);
  dm_mpoint = ctx->dm_particles;
  ierr = DMStagGetBoundaryTypes(dm_vp,&boundaryTypes[0],&boundaryTypes[1],NULL);CHKERRQ(ierr);
  if (boundaryTypes[1] == DM_BOUNDARY_PERIODIC) SETERRQ(PetscObjectComm((PetscObject)dm_vp),PETSC_ERR_SUP,"Periodic boundary conditions in y-direction not supported");
  periodicx = boundaryTypes[0] == DM_BOUNDARY_PERIODIC;

  ierr = DMStagGetProductCoordinateArraysRead(dm_vp,&cArrX,&cArrY,NULL);CHKERRQ(ierr);
  ierr = DMStagGetProductCoordinateLocationSlot(dm_vp,DMSTAG_ELEMENT,&iCenter);CHKERRQ(ierr);
  ierr = DMStagGetProductCoordinateLocationSlot(dm_vp,DMSTAG_LEFT,&iPrev);CHKERRQ(ierr);
  ierr = DMStagGetProductCoordinateLocationSlot(dm_vp,DMSTAG_RIGHT,&iNext);CHKERRQ(ierr);

  ierr = DMStagGetLocationSlot(dm_vp,DMSTAG_LEFT,0,&iVxLeft);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dm_vp,DMSTAG_RIGHT,0,&iVxRight);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dm_vp,DMSTAG_DOWN,0,&iVyDown);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dm_vp,DMSTAG_UP,0,&iVyUp);CHKERRQ(ierr);

  ierr = DMGetLocalVector(dm_vp,&vp_l);CHKERRQ(ierr);
  ierr = DMGlobalToLocalBegin(dm_vp,vp,INSERT_VALUES,vp_l);CHKERRQ(ierr);
  ierr = DMGlobalToLocalEnd(dm_vp,vp,INSERT_VALUES,vp_l);CHKERRQ(ierr);
  ierr = DMStagVecGetArrayRead(dm_vp,vp_l,&LA_vp);CHKERRQ(ierr);

  ierr = DMStagGetLocalSizes(dm_vp,&n[0],&n[1],NULL);CHKERRQ(ierr);
  ierr = DMSwarmGetLocalSize(dm_mpoint,&npoints);CHKERRQ(ierr);
  ierr = DMSwarmGetField(dm_mpoint,DMSwarmPICField_coor,NULL,NULL,(void**)&mpfield_coor);CHKERRQ(ierr);
  ierr = DMSwarmGetField(dm_mpoint,DMSwarmPICField_cellid,NULL,NULL,(void**)&mpfield_cell);CHKERRQ(ierr);
  for (p=0; p<npoints; p++) {
    PetscReal   *coor_p;
    PetscScalar vel_p[2],vLeft,vRight,vUp,vDown;
    PetscScalar x0[2],dx[2],xloc_p[2],xi_p[2];
    PetscInt    ind[2];

    e       = mpfield_cell[p];
    coor_p  = &mpfield_coor[2*p];
    ierr = DMStagGetLocalElementGlobalIndices(dm_vp,e,ind);CHKERRQ(ierr);

    /* compute local coordinates: (xp-x0)/dx = (xip+1)/2 */
    x0[0] = cArrX[ind[0]][iPrev];
    x0[1] = cArrY[ind[1]][iPrev];

    dx[0] = cArrX[ind[0]][iNext] - x0[0];
    dx[1] = cArrY[ind[1]][iNext] - x0[1];

    xloc_p[0] = (coor_p[0] - x0[0])/dx[0];
    xloc_p[1] = (coor_p[1] - x0[1])/dx[1];

    /* Checks (xi_p is only used for this, here) */
    xi_p[0] = 2.0 * xloc_p[0] -1.0;
    xi_p[1] = 2.0 * xloc_p[1] -1.0;
    if (PetscRealPart(xi_p[0]) < -1.0) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_SUP,"value (xi) too small %1.4e [e=%D]\n",(double)PetscRealPart(xi_p[0]),e);
    if (PetscRealPart(xi_p[0]) >  1.0) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_SUP,"value (xi) too large %1.4e [e=%D]\n",(double)PetscRealPart(xi_p[0]),e);
    if (PetscRealPart(xi_p[1]) < -1.0) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_SUP,"value (eta) too small %1.4e [e=%D]\n",(double)PetscRealPart(xi_p[1]),e);
    if (PetscRealPart(xi_p[1]) >  1.0) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_SUP,"value (eta) too large %1.4e [e=%D]\n",(double)PetscRealPart(xi_p[1]),e);

    /* interpolate velocity */
    vLeft  = LA_vp[ind[1]][ind[0]][iVxLeft];
    vRight = LA_vp[ind[1]][ind[0]][iVxRight];
    vUp    = LA_vp[ind[1]][ind[0]][iVyUp];
    vDown  = LA_vp[ind[1]][ind[0]][iVyDown];
    vel_p[0] = xloc_p[0]*vRight + (1.0-xloc_p[0])*vLeft;
    vel_p[1] = xloc_p[1]*vUp    + (1.0-xloc_p[1])*vDown;

    /* Update Coordinates */
    coor_p[0] += dt * PetscRealPart(vel_p[0]);
    coor_p[1] += dt * PetscRealPart(vel_p[1]);

    /* Wrap in periodic directions */
    if (periodicx) {
      coor_p[0] = RangeMod(coor_p[0],ctx->xmin,ctx->xmax);CHKERRQ(ierr);
    }
  }

  ierr = DMStagRestoreProductCoordinateArraysRead(dm_vp,&cArrX,&cArrY,NULL);CHKERRQ(ierr);
  ierr = DMSwarmRestoreField(dm_mpoint,DMSwarmPICField_cellid,NULL,NULL,(void**)&mpfield_cell);CHKERRQ(ierr);
  ierr = DMSwarmRestoreField(dm_mpoint,DMSwarmPICField_coor,NULL,NULL,(void**)&mpfield_coor);CHKERRQ(ierr);
  ierr = DMStagVecRestoreArrayRead(dm_vp,vp_l,&LA_vp);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(dm_vp,&vp_l);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
