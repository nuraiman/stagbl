static char help[] = " Extension of toy2, which relies on a branch of PETSc including DMStag,\n\
                      and adds very simple particle advection using DMSwarm\n\
                      -nx,-ny : number of cells in x and y direction\n\
                      -xmax,-ymax : domain sizes (m) \n\
                      -p : particles per cell per dimension\n\
                      -jiggle 0 : don't randomly displace initial particle positions\n\
                      -dt : timestep for simple advection\n\
                      -isoviscous : don't use variable viscosity\n";

#include <petsc.h>
#include "ctx.h"
#include "system.h"
#include "output.h"
#include "system.h"

// TODO: introduce interface to fix this (or decide that the approach here
//       is in general hacky and move more into the DMStag impl)
#include <petsc/private/dmstagimpl.h>

/* =========================================================================== */
int main(int argc, char **argv)
{
  PetscErrorCode ierr;
  Ctx            ctx;
  DM             stokesGrid,paramGrid,elementOnlyGrid,vertexOnlyGrid,swarm,daVertex,daVertex2;
  Vec            paramLocal;
  Mat            A;
  Vec            b;
  Vec            x;
  Vec            vVertex,vVertexLocal;
  KSP            ksp;
  PC             pc;
  PetscMPIInt    size;
  PetscScalar    pMin,pMax,pPrimeMin,pPrimeMax,vxInterpMin,vxInterpMax,vyInterpMin,vyInterpMax;

  /* --- Initialize and Create Context --------------------------------------- */
  ierr = PetscInitialize(&argc,&argv,(char*)0,help);CHKERRQ(ierr);
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRQ(ierr);
  ierr = CreateCtx(&ctx);CHKERRQ(ierr);

  /* --- Create Main DMStag Objects ------------------------------------------ */
  // A DMStag to hold the unknowns to solve the Stokes problem
  ierr = DMStagCreate2d(PETSC_COMM_WORLD, 
      DM_BOUNDARY_NONE,DM_BOUNDARY_NONE,       // no special boundary support yet
      ctx->M,ctx->N,PETSC_DECIDE,PETSC_DECIDE, // sizes provided as DMDA (global x, global y, local x, local y)
      0,1,1,                                   // no dof per vertex: 0 per vertex, 1 per edge, 1 per face/element
      DMSTAG_GHOST_STENCIL_BOX,                // elementwise stencil pattern
      1,                                       // elementwise stencil width
      &ctx->stokesGrid);
  stokesGrid = ctx->stokesGrid;
  ierr = DMSetUp(stokesGrid);CHKERRQ(ierr);
  ierr = DMStagSetUniformCoordinates(stokesGrid,ctx->xmin,ctx->xmax,ctx->ymin,ctx->ymax,0.0,0.0);CHKERRQ(ierr); 

  // A DMStag to hold the coefficient fields for the Stokes problem: 1 dof per vertex and two per element/face
  ierr = DMStagCreate2d(PETSC_COMM_WORLD,DM_BOUNDARY_NONE,DM_BOUNDARY_NONE,ctx->M,ctx->N,PETSC_DECIDE,PETSC_DECIDE,1,0,2,DMSTAG_GHOST_STENCIL_BOX,1,&ctx->paramGrid);CHKERRQ(ierr);
  paramGrid = ctx->paramGrid;
  ierr = DMSetUp(paramGrid);CHKERRQ(ierr);
  ierr = DMStagSetUniformCoordinates(paramGrid,ctx->xmin,ctx->xmax,ctx->ymin,ctx->ymax,0.0,0.0);CHKERRQ(ierr); 

  PetscPrintf(PETSC_COMM_WORLD,"paramGrid:\n");
  ierr = DMView(paramGrid,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);

  /* --- Create a DMDAs representing the vertices of our grid (since the logic is already there for DMSwarm) --- */
    ierr = DMStagGetVertexDMDA(paramGrid,1,&daVertex);
    ierr = DMSetUp(daVertex);CHKERRQ(ierr);
    ierr = DMDASetUniformCoordinates(daVertex,ctx->xmin,ctx->xmax,ctx->ymin,ctx->ymax,0.0,0.0);CHKERRQ(ierr);
    ierr = DMStagGetVertexDMDA(paramGrid,2,&daVertex2);
    ierr = DMSetUp(daVertex2);CHKERRQ(ierr);

    ierr = DMView(daVertex2,PETSC_VIEWER_STDOUT_WORLD);

  /* --- Create a DMSwarm (particle system) object --------------------------- */
  {
    PetscInt particlesPerElementPerDim = 10;
    PetscInt n[2];

    ierr = PetscOptionsGetInt(NULL,NULL,"-p",&particlesPerElementPerDim,NULL);CHKERRQ(ierr);
    ierr = DMCreate(PETSC_COMM_WORLD,&swarm);CHKERRQ(ierr);
    ierr = DMSetType(swarm,DMSWARM);CHKERRQ(ierr);
    ierr = DMSetDimension(swarm,2);CHKERRQ(ierr);
    ierr = DMSwarmSetType(swarm,DMSWARM_PIC);CHKERRQ(ierr);
    ierr = DMSwarmSetCellDM(swarm,daVertex);CHKERRQ(ierr);
    ierr = DMSwarmRegisterPetscDatatypeField(swarm,"rho",1,PETSC_REAL);CHKERRQ(ierr);
    ierr = DMSwarmRegisterPetscDatatypeField(swarm,"eta",1,PETSC_REAL);CHKERRQ(ierr);
    ierr = DMSwarmFinalizeFieldRegister(swarm);CHKERRQ(ierr); 
    ierr = DMStagGetLocalSizes(paramGrid,&n[0],&n[1],NULL);CHKERRQ(ierr);
    ierr = DMSwarmSetLocalSizes(swarm,n[0]*n[1]*particlesPerElementPerDim,100);CHKERRQ(ierr);
    ierr = DMSwarmInsertPointsUsingCellDM(swarm,DMSWARMPIC_LAYOUT_REGULAR,particlesPerElementPerDim);CHKERRQ(ierr);


    // Set properties for particles.  Each particle has a viscosity and density (would probably be more efficient
    //  in this example to use material ids)
    {
      PetscReal   *array_x,*array_e,*array_r;
      PetscInt    npoints,p;

      ierr = DMSwarmGetField(swarm,DMSwarmPICField_coor,NULL,NULL,(void**)&array_x);CHKERRQ(ierr);
      ierr = DMSwarmGetField(swarm,"eta",               NULL,NULL,(void**)&array_e);CHKERRQ(ierr);
      ierr = DMSwarmGetField(swarm,"rho",               NULL,NULL,(void**)&array_r);CHKERRQ(ierr);
      ierr = DMSwarmGetLocalSize(swarm,&npoints);CHKERRQ(ierr);
      for (p = 0; p < npoints; p++) {
        PetscReal x_p[2];

        // TODO: Displace particles randomly using -jiggle setting 
        // and later call migrate to account for potential changes of domain
        // see ex70.c
        // ..

        // Get the coordinates of point p
        x_p[0] = array_x[2*p + 0];
        x_p[1] = array_x[2*p + 1];

        // Call functions to compute eta and rho at that location
        array_e[p] = getEta(ctx,x_p[0],x_p[1]);
        array_r[p] = getRho(ctx,x_p[0],x_p[1]);

      }
      ierr = DMSwarmRestoreField(swarm,"rho",NULL,NULL,(void**)&array_r);CHKERRQ(ierr);
      ierr = DMSwarmRestoreField(swarm,"eta",NULL,NULL,(void**)&array_e);CHKERRQ(ierr);
      ierr = DMSwarmRestoreField(swarm,DMSwarmPICField_coor,NULL,NULL,(void**)&array_x);CHKERRQ(ierr);
    }

    // View DMSwarm object
    ierr = DMView(swarm,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  }

  /* --- Set up Problem ------------------------------------------------------ */
  // TODO replace this with 
  // 1. Obtain vertex DMDA
  // 2. Use this as the base DM for the swarm
  // 3. Get two vectors of the projected coefficients
  // 4. Populate arrayParam from these values (and their averages) instead

  // Project from particles 
  // TODO do this each time step (but reuse these vectors and move decls up)
  {
    Vec *pfields;
    Vec etaVertex,rhoVertex;
    const char *fieldnames[]={"eta","rho"};
    const PetscBool reuse = PETSC_FALSE;

    ierr = DMSwarmProjectFields(swarm,2,fieldnames,&pfields,reuse);CHKERRQ(ierr);
    etaVertex = pfields[0];
    rhoVertex = pfields[1];
  }


  {
    typedef struct {PetscReal xCorner,yCorner,xElement,yElement;} CoordinateData; // Definitely need to provide these types somehow to the user.. (specifying them wrong leads to very annoying bugs)
    typedef struct {PetscScalar etaCorner,etaElement,rho;} ElementData; // ? What's the best way to have users to do this? Provide these before hand? 
    DM             dmc;
    Vec            coordsGlobal,coordsLocal;
    CoordinateData **arrCoords;
    ElementData    **arrParam; 
    PetscInt       start[2],n[2],i,j;

    ierr = DMGetCoordinateDM(paramGrid,&dmc);CHKERRQ(ierr);
    ierr = DMGetCoordinates(paramGrid,&coordsGlobal);CHKERRQ(ierr);

    ierr = DMCreateLocalVector(dmc,&coordsLocal);CHKERRQ(ierr);
    ierr = DMGlobalToLocalBegin(dmc,coordsGlobal,INSERT_VALUES,coordsLocal);CHKERRQ(ierr);
    ierr = DMGlobalToLocalEnd(dmc,coordsGlobal,INSERT_VALUES,coordsLocal);CHKERRQ(ierr);

    ierr = DMCreateLocalVector(paramGrid,&paramLocal);CHKERRQ(ierr);
    ierr = DMStagGetGhostCorners(paramGrid,&start[0],&start[1],NULL,&n[0],&n[1],NULL);CHKERRQ(ierr);
    ierr = DMStagVecGetArray(paramGrid,paramLocal,&arrParam);CHKERRQ(ierr);
    ierr = DMStagVecGetArrayRead(dmc,coordsLocal,&arrCoords);CHKERRQ(ierr);

    // Iterate over all elements in the ghosted region. This is redundant, but means we already have the local information for later (thus doing more computation but less communication).This is also encouraged by our conflation of the two types of ghost points, as mentioned above.
    // TODO change this with getting stuff from etaVertex and rhoVertex (and interpolating)
    for (j=start[1];j<start[1]+n[1];++j) {
      for(i=start[0]; i<start[0]+n[0]; ++i) {
        arrParam[j][i].etaCorner  = getEta(ctx,arrCoords[j][i].xCorner, arrCoords[j][i].yCorner);
        arrParam[j][i].etaElement = getEta(ctx,arrCoords[j][i].xElement,arrCoords[j][i].yElement);
        arrParam[j][i].rho        = getRho(ctx,arrCoords[j][i].xElement,arrCoords[j][i].yElement);
      }
    }

    ierr = DMStagVecRestoreArray(paramGrid,paramLocal,&arrParam);CHKERRQ(ierr);
    ierr = DMStagVecRestoreArrayRead(dmc,coordsLocal,&arrCoords);CHKERRQ(ierr);
    ierr = VecDestroy(&coordsLocal);CHKERRQ(ierr);

    ierr = DMCreateGlobalVector(paramGrid,&ctx->param);CHKERRQ(ierr);
    ierr = DMLocalToGlobalBegin(paramGrid,paramLocal,INSERT_VALUES,ctx->param);CHKERRQ(ierr);
    ierr = DMLocalToGlobalEnd(paramGrid,paramLocal,INSERT_VALUES,ctx->param);CHKERRQ(ierr);
  }

  /* --- Create and Solve Linear System -------------------------------------- */

  // TODO put this, and recomputation of rho/eta from tracers, inside the time loop
  ierr = CreateSystem(ctx,&A,&b);CHKERRQ(ierr);
  ierr = VecDuplicate(b,&x);CHKERRQ(ierr);
  ierr = KSPCreate(PETSC_COMM_WORLD,&ksp);CHKERRQ(ierr);
  ierr = KSPSetOperators(ksp,A,A);CHKERRQ(ierr);
  ierr = KSPGetPC(ksp,&pc);CHKERRQ(ierr);
  ierr = PCSetType(pc,PCLU);CHKERRQ(ierr);
  if (size == 1) {
    ierr = PCFactorSetMatSolverPackage(pc,MATSOLVERUMFPACK);CHKERRQ(ierr);
  } else {
    ierr = PCFactorSetMatSolverPackage(pc,MATSOLVERMUMPS);CHKERRQ(ierr); 
  }
  ierr = PetscOptionsSetValue(NULL,"-ksp_converged_reason","");CHKERRQ(ierr); // To get info on direct solve success
  ierr = KSPSetFromOptions(ksp);CHKERRQ(ierr);
  ierr = KSPSolve(ksp,b,x);CHKERRQ(ierr);
  {
    KSPConvergedReason reason;
    ierr = KSPGetConvergedReason(ksp,&reason);CHKERRQ(ierr);
    if (reason < 0) SETERRQ(PETSC_COMM_WORLD,PETSC_ERR_CONV_FAILED,"Linear solve failed");CHKERRQ(ierr);
  }

  // Transfer velocities to arrays living on the vertex-only DMDA
  // TODO : introduce compatibility check for this (or perhaps make it so that we can do this with a vertex-only DMStag)
  ierr = DMCreateGlobalVector(daVertex2,&vVertex);CHKERRQ(ierr);
  ierr = DMCreateLocalVector(daVertex2,&vVertexLocal);CHKERRQ(ierr);
  {
    typedef struct {PetscScalar vy,vx,p;} StokesData; 

    PetscScalar       *vArr;
    Vec               xLocal;
    const PetscScalar *arrxLocalRaw;
    const StokesData  *arrxLocal;
    PetscInt          nel,npe,eidx,nxDA,nyDA;
    const PetscInt    *element_list;
    DM_Stag           *stag; // TODO: obviously this isn't good. We shouldn't have to access this data once we have a proper API

    PetscMPIInt       rank; // debug
    DMDALocalInfo      info; // debug
    PetscInt dzdb;

    MPI_Comm_rank(PETSC_COMM_WORLD,&rank);
    stag = (DM_Stag*)paramGrid->data;

    ierr = DMCreateLocalVector(stokesGrid,&xLocal);CHKERRQ(ierr);
    ierr = DMGlobalToLocalBegin(stokesGrid,x,INSERT_VALUES,xLocal);CHKERRQ(ierr);
    ierr = DMGlobalToLocalEnd(stokesGrid,x,INSERT_VALUES,xLocal);CHKERRQ(ierr);
    ierr = VecGetArrayRead(xLocal,&arrxLocalRaw);CHKERRQ(ierr);
    arrxLocal = (StokesData*) arrxLocalRaw; // TODO this is not good enough for a user interface - think of a better way to do this, say with a DMCreateSubDM and field ISs

      ierr = DMDAGetCorners(daVertex2,NULL,NULL,NULL,&nxDA,&nyDA,NULL);CHKERRQ(ierr);
      ierr = DMDAGetLocalInfo(daVertex2,&info);CHKERRQ(ierr);
      ierr = VecGetLocalSize(vVertexLocal,&dzdb);CHKERRQ(ierr);
      PetscPrintf(PETSC_COMM_WORLD,"DA local sizes: %d %d\n Local vec size %d\n",info.gxm,info.gym,dzdb);CHKERRQ(ierr);

    ierr = DMDAGetElements(daVertex2,&nel,&npe,&element_list);CHKERRQ(ierr);
    ierr = DMDAVecGetArray(daVertex2,vVertexLocal,&vArr);CHKERRQ(ierr);
    for (eidx = 0; eidx < nel; eidx++) {

      // TODO : check the hell out of this conversion, from DMDA elements to DMStag elements - check all the numbers on up to 4 procs.

      PetscInt localRowDA,localColDA;


      // This lists the local element numbers
      const PetscInt *element = &element_list[npe*eidx];
      const PetscInt NSD = 2;

      localRowDA = eidx % nxDA;
      localColDA = eidx / nxDA; // integer div
      PetscPrintf(PETSC_COMM_SELF,"[%d] DMDA el %d (%d,%d) : %d %d %d %d\n",rank,eidx,localRowDA,localColDA,element[0],element[1],element[2],element[3]);

#if 1
      vArr[element[0]*NSD+0] = arrxLocalRaw[(localRowDA  ) * stag->entriesPerElementRowGhost + (localColDA  ) * stag->entriesPerElement+ 1]; // vx is the second entry
      vArr[element[0]*NSD+1] = arrxLocalRaw[(localRowDA  ) * stag->entriesPerElementRowGhost + (localColDA  ) * stag->entriesPerElement+ 0]; // vy is the second entry

      vArr[element[1]*NSD+0] = arrxLocalRaw[(localRowDA  ) * stag->entriesPerElementRowGhost + (localColDA+1) * stag->entriesPerElement+ 1]; // vx is the second entry
      vArr[element[1]*NSD+1] = arrxLocalRaw[(localRowDA  ) * stag->entriesPerElementRowGhost + (localColDA+1) * stag->entriesPerElement+ 0]; // vx is the second entry

      // This is top-right in DMDA ordering
      vArr[element[2]*NSD+0] = arrxLocalRaw[(localRowDA+1) * stag->entriesPerElementRowGhost + (localColDA+1) * stag->entriesPerElement+ 1]; // vx is the second entry
      vArr[element[2]*NSD+1] = arrxLocalRaw[(localRowDA+1) * stag->entriesPerElementRowGhost + (localColDA+1) * stag->entriesPerElement+ 0]; // vx is the second entry

      // This is top-left in DMDA ordering
      vArr[element[3]*NSD+0] = arrxLocalRaw[(localRowDA+1) * stag->entriesPerElementRowGhost + (localColDA  ) * stag->entriesPerElement+ 1]; // vx is the second entry
      vArr[element[3]*NSD+1] = arrxLocalRaw[(localRowDA+1) * stag->entriesPerElementRowGhost + (localColDA  ) * stag->entriesPerElement+ 0]; // vx is the second entry
#endif
    }

    ierr = DMDAVecRestoreArray(daVertex2,vVertexLocal,&vArr);CHKERRQ(ierr);

    MPI_Barrier(PETSC_COMM_WORLD);
    PetscPrintf(PETSC_COMM_WORLD,"vVertexLocal:\n");
    VecView(vVertexLocal,PETSC_VIEWER_STDOUT_WORLD);
    ierr = DMLocalToGlobalBegin(daVertex2,vVertexLocal,INSERT_VALUES,vVertex);CHKERRQ(ierr);
    ierr = DMLocalToGlobalEnd(daVertex2,vVertexLocal,INSERT_VALUES,vVertex);CHKERRQ(ierr);
    PetscPrintf(PETSC_COMM_WORLD,"vVertex:\n");
    VecView(vVertex,PETSC_VIEWER_STDOUT_WORLD);

    ierr = VecRestoreArrayRead(xLocal,&arrxLocalRaw);CHKERRQ(ierr);
    ierr = VecDestroy(&xLocal);CHKERRQ(ierr);
  }

  /* --- Push Particles and Dump Xdmf ---------------------------------------- */
  // A naive forward Euler step 
  // Note: open these in Paraview 5.3.0 on OS X with "Xdmf Reader", not "Xdmf 3.0 reader"
  {
    typedef struct {PetscScalar vy,vx,p;} StokesData; 
    PetscInt          step,Np,p;
    char              filename[PETSC_MAX_PATH_LEN];
    const PetscInt    dim = 2;
    PetscReal         *coords;
    PetscInt          *element;
    PetscInt          nSteps;
    Vec               xLocal;
    const PetscScalar *arrxLocalRaw;
    const StokesData  *arrxLocal;
    PetscReal         dt;

    // TODO : change this to be as in ex70 (using the DMDA stuff and the array of vels we calculated above)

    step = 0;
    nSteps = 10;
    ierr = PetscPrintf(PETSC_COMM_WORLD,"-- Advection -----\n",step);CHKERRQ(ierr);
    ierr = PetscOptionsGetInt(NULL,NULL,"-nsteps",&nSteps,NULL);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Step %D of %D\n",step,nSteps);CHKERRQ(ierr);
    ierr = DMSwarmViewXDMF(swarm,"swarm_0000.xmf");CHKERRQ(ierr);
    for (step=1; step<=nSteps; ++step) {
      ierr = PetscPrintf(PETSC_COMM_WORLD,"Step %D of %D\n",step,nSteps);CHKERRQ(ierr); // carriage return, clear line before printing

      ierr = DMCreateLocalVector(stokesGrid,&xLocal);CHKERRQ(ierr);
      ierr = DMGlobalToLocalBegin(stokesGrid,x,INSERT_VALUES,xLocal);CHKERRQ(ierr);
      ierr = DMGlobalToLocalEnd(stokesGrid,x,INSERT_VALUES,xLocal);CHKERRQ(ierr);

      ierr = VecGetArrayRead(xLocal,&arrxLocalRaw);CHKERRQ(ierr);

      // Here we are using local element numbering, so we reinterpret the local vector ourselves
      arrxLocal = (StokesData*) arrxLocalRaw; // TODO this is not good enough for a user interface - think of a better way to do this, say with a DMCreateSubDM and field ISs

      // TODO pick a timestep based on max grid velocity

      // Advect
      ierr = DMSwarmGetLocalSize(swarm, &Np);CHKERRQ(ierr);
      ierr = DMSwarmGetField(swarm,DMSwarmPICField_coor,  NULL,NULL,(void**)&coords );CHKERRQ(ierr);
      ierr = DMSwarmGetField(swarm,DMSwarmPICField_cellid,NULL,NULL,(void**)&element);CHKERRQ(ierr); // element numbers include ghost/dummy elements

      PetscInt start[2],startGhost[2],nGhost[2],n[2],iGhostOffset,jGhostOffset;

      ierr = DMStagGetCorners(stokesGrid,&start[0],&start[1],NULL,&n[0],&n[1],NULL,NULL,NULL,NULL);CHKERRQ(ierr);

      ierr = DMStagGetGhostCorners(stokesGrid,&startGhost[0],&startGhost[1],NULL,&nGhost[0],&nGhost[1],NULL);CHKERRQ(ierr);
      iGhostOffset = startGhost[0] - start[0]; jGhostOffset = startGhost[1] - start[1];
      dt = 1e11;
      ierr = PetscOptionsGetReal(NULL,NULL,"-dt",&dt,NULL);CHKERRQ(ierr);
      for (p = 0; p<Np; ++p) {
        // We apply a simple averaged velocity to all points in each element. 
        // TODO use proper interpolation scheme to get velocity (8.19 seems odd, since we don't have velocities at the corner nodes, but rather at the boundaries..)
        const PetscReal velx = 0.5* (arrxLocal[element[p]].vx + arrxLocal[element[p]+1].vx);
        const PetscReal vely = 0.5 *(arrxLocal[element[p]].vy + arrxLocal[element[p] + nGhost[0]].vy);
        coords[p*dim+0] += dt * velx;
        coords[p*dim+1] += dt * vely;
      }
      ierr = DMSwarmRestoreField(swarm, DMSwarmPICField_coor, NULL, NULL, (void **) &coords);CHKERRQ(ierr);
      ierr = DMSwarmRestoreField(swarm,DMSwarmPICField_cellid,NULL,NULL,(void**)&element);CHKERRQ(ierr);

      /* Migrate
         I believe that this sends all points that aren't in a local cell to *all* 
         neighboring ranks, which then check to see if that point exists there. We 
         could modify things to be more complex and directly send points to the 
         correct rank only */
      ierr = DMSwarmMigrate(swarm,PETSC_TRUE);CHKERRQ(ierr);

      // TODO allow for points outside of the domain as in 8.4 p. 121?

      // TODO update grid

      // Output
      ierr = PetscSNPrintf(filename,PETSC_MAX_PATH_LEN-1,"swarm_%.4D.xmf",step);CHKERRQ(ierr);
      ierr = DMSwarmViewXDMF(swarm,filename);CHKERRQ(ierr);
    }

    ierr = VecRestoreArrayRead(xLocal,&arrxLocalRaw);CHKERRQ(ierr);
    ierr = VecDestroy(&xLocal);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_WORLD,"\rDone (%d steps).\n",nSteps);CHKERRQ(ierr);
  }

  /* --- Additional Output --------------------------------------------------- */
  // Output the system as binary with headers, to see in Octave/MATLAB (see loadData.m)
  ierr = OutputMatBinary(A,"A.pbin");
  ierr = OutputVecBinary(b,"b.pbin",PETSC_FALSE);
  ierr = OutputVecBinary(x,"x.pbin",PETSC_FALSE);

  // Create auxiliary DMStag object, with one dof per element, to help with output
  ierr = DMStagCreate2d( PETSC_COMM_WORLD, DM_BOUNDARY_NONE,DM_BOUNDARY_NONE,ctx->M,ctx->N,PETSC_DECIDE,PETSC_DECIDE,0,0,1,DMSTAG_GHOST_STENCIL_BOX,1,&elementOnlyGrid);CHKERRQ(ierr);
  ierr = DMSetUp(elementOnlyGrid);CHKERRQ(ierr);
  ierr = DMStagSetUniformCoordinates(elementOnlyGrid,ctx->xmin,ctx->xmax,ctx->ymin,ctx->ymax,0.0,0.0);CHKERRQ(ierr); 

  // Another auxiliary DMStag to help dumping vertex/corner - only data
  ierr = DMStagCreate2d(PETSC_COMM_WORLD,DM_BOUNDARY_NONE,DM_BOUNDARY_NONE,ctx->M,ctx->N,PETSC_DECIDE,PETSC_DECIDE,1,0,0,DMSTAG_GHOST_STENCIL_BOX,1,&vertexOnlyGrid);CHKERRQ(ierr);
  ierr = DMSetUp(vertexOnlyGrid);CHKERRQ(ierr);
  ierr = DMStagSetUniformCoordinates(vertexOnlyGrid,ctx->xmin,ctx->xmax,ctx->ymin,ctx->ymax,0.0,0.0);CHKERRQ(ierr); 

  // Create single dof element- or vertex-only vectors to dump with our Xdmf
  {
    typedef struct {PetscScalar etaCorner,etaElement,rho;} ParamData; // Design question: What's the best way to have users to do this? Provide these beforehand somehow?
    typedef struct {PetscScalar vy,vx,p;} StokesData; 
    Vec         xLocal;
    Vec         etaElGlobal,etaElNatural,etaElLocal;
    Vec         rhoGlobal,rhoNatural,rhoLocal;
    Vec         pGlobal,pLocal,pNatural;
    Vec         vxInterpGlobal,vxInterpLocal,vxInterpNatural,vyInterpGlobal,vyInterpLocal,vyInterpNatural;
    PetscScalar **arrRho,**arrEtaEl,**arrp,**arrvxinterp,**arrvyinterp;
    ParamData   **arrParam; 
    StokesData  **arrx;
    PetscInt    start[2],n[2],N[2],i,j;
    PetscMPIInt rank;

    MPI_Comm_rank(PETSC_COMM_WORLD,&rank);

    // TODO add checks that all these DMs are "compatible" to assure that simultaneous iteration is safe

    ierr = DMCreateLocalVector(stokesGrid,&xLocal);CHKERRQ(ierr);
    ierr = DMGlobalToLocalBegin(stokesGrid,x,INSERT_VALUES,xLocal);CHKERRQ(ierr);
    ierr = DMGlobalToLocalEnd(stokesGrid,x,INSERT_VALUES,xLocal);CHKERRQ(ierr);

    ierr = DMStagCreateNaturalVector(elementOnlyGrid,&etaElNatural);CHKERRQ(ierr);
    ierr = DMCreateLocalVector(elementOnlyGrid,&etaElLocal);CHKERRQ(ierr);

    ierr = DMStagCreateNaturalVector(elementOnlyGrid,&rhoNatural);CHKERRQ(ierr);
    ierr = DMCreateLocalVector(elementOnlyGrid,&rhoLocal);CHKERRQ(ierr);

    ierr = DMCreateLocalVector(      elementOnlyGrid,&pLocal          );CHKERRQ(ierr);
    ierr = DMCreateLocalVector(      elementOnlyGrid,&vxInterpLocal   );CHKERRQ(ierr);
    ierr = DMCreateLocalVector(      elementOnlyGrid,&vyInterpLocal   );CHKERRQ(ierr);
    ierr = DMStagCreateNaturalVector(elementOnlyGrid,&pNatural        );CHKERRQ(ierr);
    ierr = DMStagCreateNaturalVector(elementOnlyGrid,&vxInterpNatural);CHKERRQ(ierr);
    ierr = DMStagCreateNaturalVector(elementOnlyGrid,&vyInterpNatural);CHKERRQ(ierr);

    ierr = DMStagVecGetArrayRead(paramGrid,paramLocal,&arrParam);CHKERRQ(ierr);
    ierr = DMStagVecGetArray(elementOnlyGrid,etaElLocal,&arrEtaEl);CHKERRQ(ierr);
    ierr = DMStagVecGetArray(elementOnlyGrid,rhoLocal,&arrRho);CHKERRQ(ierr);
    ierr = DMStagVecGetArray(elementOnlyGrid,pLocal,&arrp);CHKERRQ(ierr);
    ierr = DMStagVecGetArray(elementOnlyGrid,vxInterpLocal,&arrvxinterp);CHKERRQ(ierr);
    ierr = DMStagVecGetArray(elementOnlyGrid,vyInterpLocal,&arrvyinterp);CHKERRQ(ierr);
    ierr = DMStagVecGetArrayRead(stokesGrid,xLocal,&arrx);CHKERRQ(ierr); // should be Read

    ierr = DMStagGetCorners(paramGrid,&start[0],&start[1],NULL,&n[0],&n[1],NULL,NULL,NULL,NULL);CHKERRQ(ierr);
    ierr = DMStagGetGlobalSizes(paramGrid,&N[0],&N[1],NULL);CHKERRQ(ierr);
    for (j=start[1]; j<start[1]+n[1]; ++j) {
      for(i=start[0]; i<start[0]+n[0]; ++i) {
        arrEtaEl[j][i]     = arrParam[j][i].etaElement;
        arrRho[j][i]       = arrParam[j][i].rho;
        arrvyinterp[j][i]  = 0.5*(arrx[j][i].vy + arrx[j+1][i].vy);
        arrvxinterp[j][i]  = 0.5*(arrx[j][i].vx + arrx[j][i+1].vx);
        arrp[j][i]         = arrx[j][i].p;
      }
    }

    ierr = DMStagVecRestoreArrayRead(paramGrid,paramLocal,&arrParam);CHKERRQ(ierr);
    ierr = DMStagVecRestoreArray(elementOnlyGrid,etaElLocal,&arrEtaEl);CHKERRQ(ierr);
    ierr = DMStagVecRestoreArray(elementOnlyGrid,rhoLocal,&arrRho);CHKERRQ(ierr);
    ierr = DMStagVecRestoreArray(elementOnlyGrid,pLocal,&arrp);CHKERRQ(ierr);
    ierr = DMStagVecRestoreArray(elementOnlyGrid,vxInterpLocal,&arrvxinterp);CHKERRQ(ierr);
    ierr = DMStagVecRestoreArray(elementOnlyGrid,vyInterpLocal,&arrvyinterp);CHKERRQ(ierr);
    ierr = DMStagVecRestoreArrayRead(stokesGrid,xLocal,&arrx);CHKERRQ(ierr);

    ierr = VecDestroy(&xLocal);CHKERRQ(ierr);

    ierr = DMCreateGlobalVector(elementOnlyGrid,&etaElGlobal);CHKERRQ(ierr);
    ierr = DMLocalToGlobalBegin(elementOnlyGrid,etaElLocal,INSERT_VALUES,etaElGlobal);CHKERRQ(ierr);
    ierr = DMLocalToGlobalEnd(elementOnlyGrid,etaElLocal,INSERT_VALUES,etaElGlobal);CHKERRQ(ierr);
    ierr = DMStagGlobalToNaturalBegin(elementOnlyGrid,etaElGlobal,INSERT_VALUES,etaElNatural);CHKERRQ(ierr);
    ierr = DMStagGlobalToNaturalEnd(elementOnlyGrid,etaElGlobal,INSERT_VALUES,etaElNatural);CHKERRQ(ierr);
    ierr = OutputVecBinary(etaElNatural,"etaElNatural.bin",PETSC_TRUE);CHKERRQ(ierr);
    ierr = VecDestroy(&etaElGlobal);CHKERRQ(ierr);
    ierr = VecDestroy(&etaElNatural);CHKERRQ(ierr);
    ierr = VecDestroy(&etaElLocal);CHKERRQ(ierr);

    ierr = DMCreateGlobalVector(elementOnlyGrid,&rhoGlobal);CHKERRQ(ierr);
    ierr = DMLocalToGlobalBegin(elementOnlyGrid,rhoLocal,INSERT_VALUES,rhoGlobal);CHKERRQ(ierr);
    ierr = DMLocalToGlobalEnd(elementOnlyGrid,rhoLocal,INSERT_VALUES,rhoGlobal);CHKERRQ(ierr);
    ierr = DMStagGlobalToNaturalBegin(elementOnlyGrid,rhoGlobal,INSERT_VALUES,rhoNatural);CHKERRQ(ierr);
    ierr = DMStagGlobalToNaturalEnd(elementOnlyGrid,rhoGlobal,INSERT_VALUES,rhoNatural);CHKERRQ(ierr);
    ierr = OutputVecBinary(rhoNatural,"rhoNatural.bin",PETSC_TRUE);CHKERRQ(ierr);
    ierr = VecDestroy(&rhoGlobal);CHKERRQ(ierr);
    ierr = VecDestroy(&rhoNatural);CHKERRQ(ierr);
    ierr = VecDestroy(&rhoLocal);CHKERRQ(ierr);

    ierr = DMCreateGlobalVector(elementOnlyGrid,&pGlobal);CHKERRQ(ierr);
    ierr = DMLocalToGlobalBegin(elementOnlyGrid,pLocal,INSERT_VALUES,pGlobal);CHKERRQ(ierr);
    ierr = DMLocalToGlobalEnd(elementOnlyGrid,pLocal,INSERT_VALUES,pGlobal);CHKERRQ(ierr);
    ierr = DMStagGlobalToNaturalBegin(elementOnlyGrid,pGlobal,INSERT_VALUES,pNatural);CHKERRQ(ierr);
    ierr = DMStagGlobalToNaturalEnd(elementOnlyGrid,pGlobal,INSERT_VALUES,pNatural);CHKERRQ(ierr);
    ierr = VecMin(pNatural,NULL,&pPrimeMin);CHKERRQ(ierr);
    ierr = VecMax(pNatural,NULL,&pPrimeMax);CHKERRQ(ierr);
    ierr = OutputVecBinary(pNatural,"pPrimeNatural.bin",PETSC_TRUE);CHKERRQ(ierr);
    ierr = VecScale(pNatural,ctx->Kcont);CHKERRQ(ierr);
    ierr = VecMin(pNatural,NULL,&pMin);CHKERRQ(ierr);
    ierr = VecMax(pNatural,NULL,&pMax);CHKERRQ(ierr);
    ierr = OutputVecBinary(pNatural,"pNatural.bin",PETSC_TRUE);CHKERRQ(ierr);
    ierr = VecDestroy(&pLocal);CHKERRQ(ierr);
    ierr = VecDestroy(&pNatural);CHKERRQ(ierr);

    ierr = DMCreateGlobalVector(elementOnlyGrid,&vxInterpGlobal);CHKERRQ(ierr);
    ierr = DMLocalToGlobalBegin(elementOnlyGrid,vxInterpLocal,INSERT_VALUES,vxInterpGlobal);CHKERRQ(ierr);
    ierr = DMLocalToGlobalEnd(elementOnlyGrid,vxInterpLocal,INSERT_VALUES,vxInterpGlobal);CHKERRQ(ierr);
    ierr = DMStagGlobalToNaturalBegin(elementOnlyGrid,vxInterpGlobal,INSERT_VALUES,vxInterpNatural);CHKERRQ(ierr);
    ierr = DMStagGlobalToNaturalEnd(elementOnlyGrid,vxInterpGlobal,INSERT_VALUES,vxInterpNatural);CHKERRQ(ierr);
    ierr = VecMin(vxInterpNatural,NULL,&vxInterpMin);CHKERRQ(ierr);
    ierr = VecMax(vxInterpNatural,NULL,&vxInterpMax);CHKERRQ(ierr);
    ierr = OutputVecBinary(vxInterpNatural,"vxInterpNatural.bin",PETSC_TRUE);CHKERRQ(ierr);
    ierr = VecDestroy(&vxInterpGlobal);CHKERRQ(ierr);
    ierr = VecDestroy(&vxInterpLocal);CHKERRQ(ierr);
    ierr = VecDestroy(&vxInterpNatural);CHKERRQ(ierr);

    ierr = DMCreateGlobalVector(elementOnlyGrid,&vyInterpGlobal);CHKERRQ(ierr);
    ierr = DMLocalToGlobalBegin(elementOnlyGrid,vyInterpLocal,INSERT_VALUES,vyInterpGlobal);CHKERRQ(ierr);
    ierr = DMLocalToGlobalEnd(elementOnlyGrid,vyInterpLocal,INSERT_VALUES,vyInterpGlobal);CHKERRQ(ierr);
    ierr = DMStagGlobalToNaturalBegin(elementOnlyGrid,vyInterpGlobal,INSERT_VALUES,vyInterpNatural);CHKERRQ(ierr);
    ierr = DMStagGlobalToNaturalEnd(elementOnlyGrid,vyInterpGlobal,INSERT_VALUES,vyInterpNatural);CHKERRQ(ierr);
    ierr = VecMin(vyInterpNatural,NULL,&vyInterpMin);CHKERRQ(ierr);
    ierr = VecMax(vyInterpNatural,NULL,&vyInterpMax);CHKERRQ(ierr);
    ierr = OutputVecBinary(vyInterpNatural,"vyInterpNatural.bin",PETSC_TRUE);CHKERRQ(ierr);
    ierr = VecDestroy(&vyInterpGlobal);CHKERRQ(ierr);
    ierr = VecDestroy(&vyInterpNatural);CHKERRQ(ierr);
    ierr = VecDestroy(&vyInterpLocal);CHKERRQ(ierr);
  }

  // Elements-only Xdmf
  {
    PetscViewer viewer;
    ierr = OutputDMCoordsNaturalBinary(elementOnlyGrid,"coordsElNatural.bin",PETSC_TRUE);CHKERRQ(ierr);
    ierr = DMStag2dXDMFStart(elementOnlyGrid,"ParaViewMe.xmf","coordsElNatural.bin",&viewer);CHKERRQ(ierr);
    ierr = DMStag2dXDMFAddAttribute(elementOnlyGrid,"etaElNatural.bin","eta",viewer);CHKERRQ(ierr);
    ierr = DMStag2dXDMFAddAttribute(elementOnlyGrid,"rhoNatural.bin","rho",viewer);CHKERRQ(ierr);
    ierr = DMStag2dXDMFAddAttribute(elementOnlyGrid,"pNatural.bin","p",viewer);CHKERRQ(ierr);
    ierr = DMStag2dXDMFAddAttribute(elementOnlyGrid,"pPrimeNatural.bin","pPrime",viewer);CHKERRQ(ierr);
    ierr = DMStag2dXDMFAddAttribute(elementOnlyGrid,"vxInterpNatural.bin","vxInterp",viewer);CHKERRQ(ierr);
    ierr = DMStag2dXDMFAddAttribute(elementOnlyGrid,"vyInterpNatural.bin","vyInterp",viewer);CHKERRQ(ierr);
    ierr = DMStag2dXDMFFinish(&viewer);CHKERRQ(ierr);
  }

  // Vertex-only Xdmf
  {
    PetscViewer viewer;
    ierr = OutputDMCoordsNaturalBinary(vertexOnlyGrid,"coordsVertexNatural.bin",PETSC_TRUE);CHKERRQ(ierr);
    ierr = DMStag2dXDMFStart(vertexOnlyGrid,"ParaViewMe_Too.xmf","coordsVertexNatural.bin",&viewer);CHKERRQ(ierr);
    ierr = DMStag2dXDMFFinish(&viewer);CHKERRQ(ierr);
  }

  /* --- Simple Diagnostics -------------------------------------------------- */
  ierr = PetscPrintf(PETSC_COMM_WORLD,"-- Info -----\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Kbound = %g\n",ctx->Kbound);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Kcont = %g\n",ctx->Kcont);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"hx = %g\n",ctx->hxCharacteristic);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"hy = %g\n",ctx->hyCharacteristic);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"-- Min / Max --\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"p' %g / %g \n",pPrimeMin,pPrimeMax);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"p  %g / %g\n",pMin,pMax);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"vxInterp %g / %g\n",vxInterpMin,vxInterpMax);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"vyInterp %g / %g\n",vyInterpMin,vyInterpMax);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"-------------\n");CHKERRQ(ierr);

  /* --- Clean up and Finalize ----------------------------------------------- */
  ierr = DMDestroy(&daVertex);CHKERRQ(ierr);
  ierr = DMDestroy(&daVertex2);CHKERRQ(ierr);
  ierr = DMDestroy(&elementOnlyGrid);CHKERRQ(ierr);
  ierr = DMDestroy(&vertexOnlyGrid);CHKERRQ(ierr);
  ierr = VecDestroy(&paramLocal);CHKERRQ(ierr);
  ierr = MatDestroy(&A);CHKERRQ(ierr);
  ierr = VecDestroy(&b);CHKERRQ(ierr);
  ierr = VecDestroy(&x);CHKERRQ(ierr);
  ierr = VecDestroy(&vVertex);CHKERRQ(ierr);
  ierr = VecDestroy(&vVertexLocal);CHKERRQ(ierr);
  ierr = KSPDestroy(&ksp);CHKERRQ(ierr);
  ierr = DestroyCtx(&ctx);CHKERRQ(ierr);
  ierr = PetscFinalize();
  return ierr;
}
