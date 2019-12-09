#include "dump.h"

PetscErrorCode DumpParticles(Ctx ctx,PetscInt timestep)
{
  PetscErrorCode ierr;
  char           filename[PETSC_MAX_PATH_LEN];

  PetscFunctionBeginUser;
  ierr = PetscSNPrintf(filename,PETSC_MAX_PATH_LEN-1,"particles_%.4D.xmf",timestep);CHKERRQ(ierr);
  ierr = DMSwarmViewXDMF(ctx->dm_particles,filename);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode DumpStokes(Ctx ctx,PetscInt timestep)
{
  PetscErrorCode ierr;
  PetscInt       dim;
  DM             dm_stokes,dm_coefficients;
  DM             dm_velocity_average;
  Vec            vec_stokes, vec_velocity_averaged;
  DM             da_velocity_averaged,da_P,da_eta_element,da_eta_corner,da_rho;
  Vec            vec_velocity_averaged_da,vec_P,vec_eta_element,vec_eta_corner,vec_rho;
  Vec            vec_coefficients,vec_coefficients_local;
  PetscInt       ex,ey,ez,startx,starty,startz,nx,ny,nz;
  Vec            vec_stokes_local,vec_velocity_averaged_da_local;
  PetscInt       slot_vx_left,slot_vx_right,slot_vy_down,slot_vy_up,slot_vx_center,slot_vy_center,slot_vz_center,slot_vz_back,slot_vz_front;

  PetscFunctionBeginUser;
  ierr = StagBLArrayPETScGetGlobalVec(ctx->stokes_array,&vec_stokes);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)vec_stokes,"solution");CHKERRQ(ierr);
  ierr = StagBLGridPETScGetDM(ctx->stokes_grid,&dm_stokes);CHKERRQ(ierr);
  ierr = DMGetDimension(dm_stokes,&dim);CHKERRQ(ierr);
  ierr = StagBLGridPETScGetDM(ctx->coefficient_grid,&dm_coefficients);CHKERRQ(ierr);
  ierr = StagBLArrayPETScGetLocalVec(ctx->coefficient_array,&vec_coefficients_local);CHKERRQ(ierr);

  switch (dim) {
    case 2:
      ierr = DMStagCreateCompatibleDMStag(dm_stokes,0,0,2,0,&dm_velocity_average); /* 2 dof per element */
      break;
    case 3:
      ierr = DMStagCreateCompatibleDMStag(dm_stokes,0,0,0,3,&dm_velocity_average); /* 3 dof per element */
      break;
    default: StagBLError1(PetscObjectComm((PetscObject)dm_stokes),"Unsupported dimension %D",dim);
  }
  ierr = DMSetUp(dm_velocity_average);CHKERRQ(ierr);
  ierr = DMStagSetUniformCoordinatesProduct(dm_velocity_average,ctx->xmin,ctx->xmax,ctx->ymin,ctx->ymax,ctx->zmin,ctx->zmax);CHKERRQ(ierr);
  ierr = DMCreateGlobalVector(dm_velocity_average,&vec_velocity_averaged);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dm_stokes,DMSTAG_LEFT,   0,&slot_vx_left);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dm_stokes,DMSTAG_RIGHT,  0,&slot_vx_right);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dm_stokes,DMSTAG_DOWN,   0,&slot_vy_down);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dm_stokes,DMSTAG_UP,     0,&slot_vy_up);CHKERRQ(ierr);
  if (dim == 3) {
    ierr = DMStagGetLocationSlot(dm_stokes,DMSTAG_FRONT,0,&slot_vz_front);CHKERRQ(ierr);
    ierr = DMStagGetLocationSlot(dm_stokes,DMSTAG_BACK, 0,&slot_vz_back);CHKERRQ(ierr);
  }
  ierr = DMStagGetLocationSlot(dm_velocity_average,DMSTAG_ELEMENT,0,&slot_vx_center);CHKERRQ(ierr);
  ierr = DMStagGetLocationSlot(dm_velocity_average,DMSTAG_ELEMENT,1,&slot_vy_center);CHKERRQ(ierr);
  if (dim == 3) {
    ierr = DMStagGetLocationSlot(dm_velocity_average,DMSTAG_ELEMENT,2,&slot_vz_center);CHKERRQ(ierr);
  }
  ierr = DMStagGetCorners(dm_velocity_average,&startx,&starty,&startz,&nx,&ny,&nz,NULL,NULL,NULL);CHKERRQ(ierr);
  ierr = DMGetLocalVector(dm_stokes,&vec_stokes_local);CHKERRQ(ierr);
  ierr = DMGetLocalVector(dm_velocity_average,&vec_velocity_averaged_da_local);CHKERRQ(ierr);
  ierr = DMGlobalToLocal(dm_stokes,vec_stokes,INSERT_VALUES,vec_stokes_local);CHKERRQ(ierr);
  if (dim == 2) {
    PetscScalar    ***arrStokes,***arrVelAvg;

    ierr = DMStagVecGetArrayRead(dm_stokes,vec_stokes_local,&arrStokes);CHKERRQ(ierr);
    ierr = DMStagVecGetArray(    dm_velocity_average,vec_velocity_averaged_da_local,&arrVelAvg);CHKERRQ(ierr);
    for (ey = starty; ey<starty+ny; ++ey) {
      for (ex = startx; ex<startx+nx; ++ex) {
        arrVelAvg[ey][ex][slot_vx_center] = 0.5 * (arrStokes[ey][ex][slot_vx_left] + arrStokes[ey][ex][slot_vx_right]);
        arrVelAvg[ey][ex][slot_vy_center] = 0.5 * (arrStokes[ey][ex][slot_vy_down] + arrStokes[ey][ex][slot_vy_up]);
      }
    }
    ierr = DMStagVecRestoreArrayRead(dm_stokes,vec_stokes_local,&arrStokes);CHKERRQ(ierr);
    ierr = DMStagVecRestoreArray(   dm_velocity_average, vec_velocity_averaged_da_local,&arrVelAvg);CHKERRQ(ierr);
  } else if (dim == 3) {
    PetscScalar    ****arrStokes,****arrVelAvg;

    ierr = DMStagVecGetArrayRead(dm_stokes,vec_stokes_local,&arrStokes);CHKERRQ(ierr);
    ierr = DMStagVecGetArray(    dm_velocity_average,vec_velocity_averaged_da_local,&arrVelAvg);CHKERRQ(ierr);
    for (ez = startz; ez<startz+nz; ++ez) {
      for (ey = starty; ey<starty+ny; ++ey) {
        for (ex = startx; ex<startx+nx; ++ex) {
          arrVelAvg[ez][ey][ex][slot_vx_center] = 0.5 * (arrStokes[ez][ey][ex][slot_vx_left] + arrStokes[ez][ey][ex][slot_vx_right]);
          arrVelAvg[ez][ey][ex][slot_vy_center] = 0.5 * (arrStokes[ez][ey][ex][slot_vy_down] + arrStokes[ez][ey][ex][slot_vy_up]);
          arrVelAvg[ez][ey][ex][slot_vz_center] = 0.5 * (arrStokes[ez][ey][ex][slot_vz_back] + arrStokes[ez][ey][ex][slot_vz_front]);
        }
      }
    }
    ierr = DMStagVecRestoreArrayRead(dm_stokes,vec_stokes_local,&arrStokes);CHKERRQ(ierr);
    ierr = DMStagVecRestoreArray(   dm_velocity_average, vec_velocity_averaged_da_local,&arrVelAvg);CHKERRQ(ierr);
  } else StagBLError1(PetscObjectComm((PetscObject)dm_stokes),"Unsupported dimension %D",dim);
  ierr = DMLocalToGlobal(dm_velocity_average,vec_velocity_averaged_da_local,INSERT_VALUES,vec_velocity_averaged);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(dm_stokes,&vec_stokes_local);CHKERRQ(ierr);
  ierr = DMRestoreLocalVector(dm_velocity_average,&vec_velocity_averaged_da_local);CHKERRQ(ierr);

  /* Create a global coefficient vector (otherwise not needed) */
  ierr = DMGetGlobalVector(dm_coefficients,&vec_coefficients);CHKERRQ(ierr);
  ierr = DMLocalToGlobal(dm_coefficients,vec_coefficients_local,INSERT_VALUES,vec_coefficients);CHKERRQ(ierr);

  /* Split to DMDAs */
  ierr = DMStagVecSplitToDMDA(dm_stokes,vec_stokes,DMSTAG_ELEMENT,0,&da_P,&vec_P);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)vec_P,"p (scaled)");CHKERRQ(ierr);
  if (dim == 2) {
    ierr = DMStagVecSplitToDMDA(dm_coefficients,vec_coefficients,DMSTAG_DOWN_LEFT,0,&da_eta_corner,&vec_eta_corner);CHKERRQ(ierr);
    ierr = PetscObjectSetName((PetscObject)vec_eta_corner,"eta");CHKERRQ(ierr);
    ierr = DMStagVecSplitToDMDA(dm_coefficients,vec_coefficients,DMSTAG_DOWN_LEFT,1,&da_rho,&vec_rho);CHKERRQ(ierr);
    ierr = PetscObjectSetName((PetscObject)vec_rho,"density");CHKERRQ(ierr);
  }
  ierr = DMStagVecSplitToDMDA(dm_coefficients,vec_coefficients,DMSTAG_ELEMENT,0,&da_eta_element,&vec_eta_element);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)vec_eta_element,"eta");CHKERRQ(ierr);
  ierr = DMStagVecSplitToDMDA(dm_velocity_average,vec_velocity_averaged,DMSTAG_ELEMENT,-3,&da_velocity_averaged,&vec_velocity_averaged_da);CHKERRQ(ierr); /* note -3 : pad with zero */
  ierr = PetscObjectSetName((PetscObject)vec_velocity_averaged_da,"Velocity (Averaged)");CHKERRQ(ierr);

  ierr = DMRestoreGlobalVector(dm_coefficients,&vec_coefficients);CHKERRQ(ierr);

  /* Dump element-based fields to a .vtr file */
  {
    PetscViewer viewer;
    char        filename[PETSC_MAX_PATH_LEN];

    ierr = PetscSNPrintf(filename,PETSC_MAX_PATH_LEN-1,"out_element_%.4D.vtr",timestep);CHKERRQ(ierr);
    ierr = PetscViewerVTKOpen(PetscObjectComm((PetscObject)da_velocity_averaged),filename,FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
    ierr = VecView(vec_velocity_averaged_da,viewer);CHKERRQ(ierr);
    ierr = VecView(vec_P,viewer);CHKERRQ(ierr);
    ierr = VecView(vec_eta_element,viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }

  /* Dump vertex-based fields to a second .vtr file */
  if (dim == 2) {
    PetscViewer viewer;
    char        filename[PETSC_MAX_PATH_LEN];

    ierr = PetscSNPrintf(filename,PETSC_MAX_PATH_LEN-1,"out_vertex_%.4D.vtr",timestep);CHKERRQ(ierr);
    ierr = PetscViewerVTKOpen(PetscObjectComm((PetscObject)da_eta_corner),filename,FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
    ierr = VecView(vec_eta_corner,viewer);CHKERRQ(ierr);
    ierr = VecView(vec_rho,viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }

  /* For testing, option to dump the solution to an ASCII file */
  if (timestep == 0) {
    PetscBool debug_ascii_dump = PETSC_FALSE;

    ierr = PetscOptionsGetBool(NULL,NULL,"-debug_ascii_dump",&debug_ascii_dump,NULL);CHKERRQ(ierr);
    if (debug_ascii_dump) {
      PetscViewer viewer;
      PetscViewerASCIIOpen(PetscObjectComm((PetscObject)da_velocity_averaged),"x.matlabascii.txt",&viewer);CHKERRQ(ierr);
      PetscViewerPushFormat(viewer,PETSC_VIEWER_ASCII_MATLAB);
      ierr = VecView(vec_stokes,viewer);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
    }
  }

  /* Destroy DMDAs and Vecs */
  ierr = VecDestroy(&vec_velocity_averaged_da);CHKERRQ(ierr);
  ierr = VecDestroy(&vec_P);CHKERRQ(ierr);
  ierr = VecDestroy(&vec_eta_element);CHKERRQ(ierr);
  if (dim == 2) {
    ierr = VecDestroy(&vec_eta_corner);CHKERRQ(ierr);
    ierr = VecDestroy(&vec_rho);CHKERRQ(ierr);
  }
  ierr = VecDestroy(&vec_velocity_averaged);CHKERRQ(ierr);
  ierr = DMDestroy(&da_velocity_averaged);CHKERRQ(ierr);
  ierr = DMDestroy(&da_P);CHKERRQ(ierr);
  ierr = DMDestroy(&da_eta_element);CHKERRQ(ierr);
  if (dim == 2) {
    ierr = DMDestroy(&da_eta_corner);CHKERRQ(ierr);
    ierr = DMDestroy(&da_rho);CHKERRQ(ierr);
  }
  ierr = DMDestroy(&dm_velocity_average);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode DumpTemperature(Ctx ctx, PetscInt timestep)
{
  PetscErrorCode ierr;
  PetscInt       dim;
  DM             dmTemp,daTemp;
  Vec            vecTemp,vecTempDa;
  PetscViewer    viewer;
  char           filename[PETSC_MAX_PATH_LEN];

  PetscFunctionBeginUser;
  ierr = StagBLArrayPETScGetGlobalVec(ctx->temperature_array,&vecTemp);CHKERRQ(ierr);
  ierr = StagBLGridPETScGetDM(ctx->temperature_grid,&dmTemp);CHKERRQ(ierr);
  ierr = DMGetDimension(dmTemp,&dim);CHKERRQ(ierr);
  if (dim != 2) StagBLError(PetscObjectComm((PetscObject)dmTemp),"Only implemented for 2D");

  /* Split to DMDA */
  ierr = DMStagVecSplitToDMDA(dmTemp,vecTemp,DMSTAG_DOWN_LEFT,0,&daTemp,&vecTempDa);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)vecTempDa,"Temperature");CHKERRQ(ierr);

  /* View */
  ierr = PetscSNPrintf(filename,PETSC_MAX_PATH_LEN-1,"out_temp_vertex_%.4D.vtr",timestep);CHKERRQ(ierr);
  ierr = PetscViewerVTKOpen(PetscObjectComm((PetscObject)daTemp),filename,FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(vecTempDa,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

  /* Destroy DMDAs and Vecs */
  ierr = VecDestroy(&vecTempDa);CHKERRQ(ierr);
  ierr = DMDestroy(&daTemp);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
