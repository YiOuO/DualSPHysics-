//HEAD_DSPH
/*
 <DUALSPHYSICS>  Copyright (c) 2020 by Dr Jose M. Dominguez et al. (see http://dual.sphysics.org/index.php/developers/). 

 EPHYSLAB Environmental Physics Laboratory, Universidade de Vigo, Ourense, Spain.
 School of Mechanical, Aerospace and Civil Engineering, University of Manchester, Manchester, U.K.

 This file is part of DualSPHysics. 

 DualSPHysics is free software: you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License 
 as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version.
 
 DualSPHysics is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details. 

 You should have received a copy of the GNU Lesser General Public License along with DualSPHysics. If not, see <http://www.gnu.org/licenses/>. 
*/

/// \file JSphCpuSingle.cpp \brief Implements the class \ref JSphCpuSingle.

#include "JSphCpuSingle.h"
#include "JCellDivCpuSingle.h"
#include "JArraysCpu.h"
#include "JSphMk.h"
#include "JPartsLoad4.h"
#include "Functions.h"
#include "FunctionsMath.h"
#include "JXml.h"
#include "JDsMotion.h"
#include "JDsViscoInput.h"
#include "JWaveGen.h"
#include "JMLPistons.h"
#include "JRelaxZones.h"
#include "JChronoObjects.h"
#include "JDsMooredFloatings.h"
#include "JDsFtForcePoints.h"
#include "JDsOutputTime.h"
#include "JTimeControl.h"
#include "JDsGaugeSystem.h"
#include "JSphInOut.h"
#include "JFtMotionSave.h" //<vs_ftmottionsv>  
#include "JLinearValue.h"
#include "JDataArrays.h"
#include "JSphShifting.h"
#include "JDsPips.h"
#include "JDsExtraData.h"
#include "FunSphEos.h"

//#include <climits>
//#include <direct.h>
//#include <io.h>
#include <string>
using namespace std;
//==============================================================================
/// Constructor.
//==============================================================================
JSphCpuSingle::JSphCpuSingle():JSphCpu(false){
  ClassName="JSphCpuSingle";
  CellDivSingle=NULL;
}

//==============================================================================
/// Destructor.
//==============================================================================
JSphCpuSingle::~JSphCpuSingle(){
  DestructorActive=true;
  delete CellDivSingle; CellDivSingle=NULL;
}

//==============================================================================
/// Return memory reserved in CPU.
/// Devuelve la memoria reservada en cpu.
//==============================================================================
llong JSphCpuSingle::GetAllocMemoryCpu()const{  
  llong s=JSphCpu::GetAllocMemoryCpu();
  //-Allocated in other objects.
  if(CellDivSingle)s+=CellDivSingle->GetAllocMemory();
  return(s);
}

//==============================================================================
/// Update maximum values of memory, particles & cells.
/// Actualiza los valores maximos de memory, particles y cells.
//==============================================================================
void JSphCpuSingle::UpdateMaxValues(){
  const llong mcpu=GetAllocMemoryCpu();
  MaxNumbers.memcpu=max(MaxNumbers.memcpu,mcpu);
  MaxNumbers.memgpu=0;
  MaxNumbers.particles=max(MaxNumbers.particles,Np);
  if(CellDivSingle)MaxNumbers.cells=max(MaxNumbers.cells,CellDivSingle->GetNct());
}

//==============================================================================
/// Load the execution configuration.
/// Carga la configuracion de ejecucion.
//==============================================================================
void JSphCpuSingle::LoadConfig(const JSphCfgRun *cfg){
  //-Load OpenMP configuraction. | Carga configuracion de OpenMP.
  ConfigOmp(cfg);
  //-Load basic general configuraction. | Carga configuracion basica general.
  JSph::LoadConfig(cfg);
  //-Checks compatibility of selected options.
  Log->Print("**Special case configuration is loaded");
}

//==============================================================================
/// Configuration of current domain.
/// Configuracion del dominio actual.
//==============================================================================
void JSphCpuSingle::ConfigDomain(){
  //-Configure cell map division (defines ScellDiv, Scell, Map_Cells). 
  ConfigCellDivision();
  //-Calculate number of particles. | Calcula numero de particulas.
  Np=PartsLoaded->GetCount(); Npb=CaseNpb; NpbOk=Npb;
  //-Allocates fixed memory for moving & floating particles. | Reserva memoria fija para moving y floating.
  AllocCpuMemoryFixed();
  //-Allocates memory in CPU for particles. | Reserva memoria en Cpu para particulas.
  AllocCpuMemoryParticles(Np,0);

  //-Copies particle data.
  ReserveBasicArraysCpu();
  memcpy(Posc,PartsLoaded->GetPos(),sizeof(tdouble3)*Np);
  memcpy(Idpc,PartsLoaded->GetIdp(),sizeof(unsigned)*Np);
  memcpy(Velrhopc,PartsLoaded->GetVelRhop(),sizeof(tfloat4)*Np);

  //-Computes radius of floating bodies.
  if(CaseNfloat && PeriActive!=0 && !PartBegin)CalcFloatingRadius(Np,Posc,Idpc);
  //-Configures floating motion data storage with high frequency. //<vs_ftmottionsv>  
  if(FtMotSave)ConfigFtMotionSave(Np,Posc,Idpc);                  //<vs_ftmottionsv>  

  //-Configures Multi-Layer Pistons according particles. | Configura pistones Multi-Layer segun particulas.
  if(MLPistons)MLPistons->PreparePiston(Dp,Np,Idpc,Posc);

  //-Load particle code. | Carga code de particulas.
  LoadCodeParticles(Np,Idpc,Codec);

  //-Load normals for boundary particles (fixed and moving).
  if(UseNormals)LoadBoundNormals(Np,Npb,Idpc,Codec,BoundNormalc);

  //-Runs initialization operations from XML.
  RunInitialize(Np,Npb,Posc,Idpc,Codec,Velrhopc,BoundNormalc);
  if(UseNormals)ConfigBoundNormals(Np,Npb,Posc,Idpc,BoundNormalc);

  //-Creates PartsInit object with initial particle data for automatic configurations.
  CreatePartsInit(Np,Posc,Codec);

  //-Computes MK domain for boundary and fluid particles.
  MkInfo->ComputeMkDomains(Np,Posc,Codec);

  //-Sets local domain of the simulation within Map_Cells and computes DomCellCode.
  //-Establece dominio de simulacion local dentro de Map_Cells y calcula DomCellCode.
  SelecDomain(TUint3(0,0,0),Map_Cells);
  //-Computes inital cell of the particles and checks if there are unexpected excluded particles.
  //-Calcula celda inicial de particulas y comprueba si hay excluidas inesperadas.
  LoadDcellParticles(Np,Codec,Posc,Dcellc);

  //-Creates object for Celldiv on the CPU and selects a valid cellmode.
  //-Crea objeto para divide en CPU y selecciona un cellmode valido.
  CellDivSingle=new JCellDivCpuSingle(Stable,FtCount!=0,PeriActive,CellDomFixed,CellMode
    ,Scell,Map_PosMin,Map_PosMax,Map_Cells,CaseNbound,CaseNfixed,CaseNpb,DirOut);
  CellDivSingle->DefineDomain(DomCellCode,DomCelIni,DomCelFin,DomPosMin,DomPosMax);
  ConfigCellDiv((JCellDivCpu*)CellDivSingle);

  ConfigSaveData(0,1,"");

  //-Reorders particles according to cells.
  //-Reordena particulas por celda.
  BoundChanged=true;
  RunCellDivide(true);
}

//==============================================================================
/// Redimension space reserved for particles in CPU, measure 
/// time consumed using TMC_SuResizeNp. On finishing, update divide.
///
/// Redimensiona el espacio reservado para particulas en CPU midiendo el
/// tiempo consumido con TMC_SuResizeNp. Al terminar actualiza el divide.
//==============================================================================
void JSphCpuSingle::ResizeParticlesSize(unsigned newsize,float oversize,bool updatedivide){
  Timersc->TmStart(TMC_SuResizeNp);
  newsize+=(oversize>0? unsigned(oversize*newsize): 0);
  ResizeCpuMemoryParticles(newsize);
  Timersc->TmStop(TMC_SuResizeNp);
  if(updatedivide)RunCellDivide(true);
}

//==============================================================================
/// Create list of new periodic particles to duplicate.
/// With stable activated reordered list of periodic particles.
///
/// Crea lista de nuevas particulas periodicas a duplicar.
/// Con stable activado reordena lista de periodicas.
//==============================================================================
unsigned JSphCpuSingle::PeriodicMakeList(unsigned n,unsigned pini,bool stable
  ,unsigned nmax,tdouble3 perinc,const tdouble3 *pos,const typecode *code
  ,unsigned *listp)const
{
  unsigned count=0;
  if(n){
    //-Initialize size of list lsph to zero. | Inicializa tamanho de lista lspg a cero.
    listp[nmax]=0;
    for(unsigned p=0;p<n;p++){
      const unsigned p2=p+pini;
      //-Keep normal or periodic particles. | Se queda con particulas normales o periodicas.
      if(CODE_GetSpecialValue(code[p2])<=CODE_PERIODIC){
        //-Get particle position. | Obtiene posicion de particula.
        const tdouble3 ps=pos[p2];
        tdouble3 ps2=ps+perinc;
        if(Map_PosMin<=ps2 && ps2<Map_PosMax){
          unsigned cp=listp[nmax]; listp[nmax]++; if(cp<nmax)listp[cp]=p2;
        }
        ps2=ps-perinc;
        if(Map_PosMin<=ps2 && ps2<Map_PosMax){
          unsigned cp=listp[nmax]; listp[nmax]++; if(cp<nmax)listp[cp]=(p2|0x80000000);
        }
      }
    }
    count=listp[nmax];
    //-Reorder list if it is valid and stability is activated. | Reordena lista si es valida y stable esta activado.
    if(stable && count && count<=nmax){
      //-Don't make mistake because at the moment the list is not created using OpenMP. | No hace falta porque de momento no se crea la lista usando OpenMP.
    }
  }
  return(count);
}

//==============================================================================
/// Duplicate the indicated particle position applying displacement.
/// Duplicated particles are considered to be always valid and are inside
/// of the domain.
/// This kernel works for single-cpu & multi-cpu because the computations are done  
/// starting from domposmin.
/// It is controlled that the coordinates of the cell do not exceed the maximum.
///
/// Duplica la posicion de la particula indicada aplicandole un desplazamiento.
/// Las particulas duplicadas se considera que siempre son validas y estan dentro
/// del dominio.
/// Este kernel vale para single-cpu y multi-cpu porque los calculos se hacen 
/// a partir de domposmin.
/// Se controla que las coordendas de celda no sobrepasen el maximo.
//==============================================================================
void JSphCpuSingle::PeriodicDuplicatePos(unsigned pnew,unsigned pcopy,bool inverse,double dx,double dy,double dz,tuint3 cellmax,tdouble3 *pos,unsigned *dcell)const{
  //-Get pos of particle to be duplicated. | Obtiene pos de particula a duplicar.
  tdouble3 ps=pos[pcopy];
  //-Apply displacement. | Aplica desplazamiento.
  ps.x+=(inverse? -dx: dx);
  ps.y+=(inverse? -dy: dy);
  ps.z+=(inverse? -dz: dz);
  //-Calculate coordinates of cell inside of domain. | Calcula coordendas de celda dentro de dominio.
  unsigned cx=unsigned((ps.x-DomPosMin.x)/Scell);
  unsigned cy=unsigned((ps.y-DomPosMin.y)/Scell);
  unsigned cz=unsigned((ps.z-DomPosMin.z)/Scell);
  //-Adjust coordinates of cell is they exceed maximum. | Ajusta las coordendas de celda si sobrepasan el maximo.
  cx=(cx<=cellmax.x? cx: cellmax.x);
  cy=(cy<=cellmax.y? cy: cellmax.y);
  cz=(cz<=cellmax.z? cz: cellmax.z);
  //-Record position and cell of new particles. |  Graba posicion y celda de nuevas particulas.
  pos[pnew]=ps;
  dcell[pnew]=DCEL_Cell(DomCellCode,cx,cy,cz);
}

//==============================================================================
/// Create periodic particles starting from a list of the particles to duplicate.
/// Assume that all the particles are valid.
/// This kernel works for single-cpu & multi-cpu because it uses domposmin.
///
/// Crea particulas periodicas a partir de una lista con las particulas a duplicar.
/// Se presupone que todas las particulas son validas.
/// Este kernel vale para single-cpu y multi-cpu porque usa domposmin. 
//==============================================================================
void JSphCpuSingle::PeriodicDuplicateVerlet(unsigned np,unsigned pini,tuint3 cellmax
  ,tdouble3 perinc,const unsigned *listp,unsigned *idp,typecode *code,unsigned *dcell
  ,tdouble3 *pos,tfloat4 *velrhop,tsymatrix3f *spstau,tfloat4 *velrhopm1)const
{
  const int n=int(np);
  #ifdef OMP_USE
    #pragma omp parallel for schedule (static) if(n>OMP_LIMIT_COMPUTELIGHT)
  #endif
  for(int p=0;p<n;p++){
    const unsigned pnew=unsigned(p)+pini;
    const unsigned rp=listp[p];
    const unsigned pcopy=(rp&0x7FFFFFFF);
    //-Adjust position and cell of new particle. | Ajusta posicion y celda de nueva particula.
    PeriodicDuplicatePos(pnew,pcopy,(rp>=0x80000000),perinc.x,perinc.y,perinc.z,cellmax,pos,dcell);
    //-Copy the rest of the values. | Copia el resto de datos.
    idp[pnew]=idp[pcopy];
    code[pnew]=CODE_SetPeriodic(code[pcopy]);
    velrhop[pnew]=velrhop[pcopy];
    velrhopm1[pnew]=velrhopm1[pcopy];
    if(spstau)spstau[pnew]=spstau[pcopy];
  }
}

//==============================================================================
/// Create periodic particles starting from a list of the particles to duplicate.
/// Assume that all the particles are valid.
/// This kernel works for single-cpu & multi-cpu because it uses domposmin.
///
/// Crea particulas periodicas a partir de una lista con las particulas a duplicar.
/// Se presupone que todas las particulas son validas.
/// Este kernel vale para single-cpu y multi-cpu porque usa domposmin. 
//==============================================================================
void JSphCpuSingle::PeriodicDuplicateSymplectic(unsigned np,unsigned pini,tuint3 cellmax,tdouble3 perinc,const unsigned *listp
  ,unsigned *idp,typecode *code,unsigned *dcell,tdouble3 *pos,tfloat4 *velrhop,tsymatrix3f *spstau,tdouble3 *pospre,tfloat4 *velrhoppre)const
{
  const int n=int(np);
  #ifdef OMP_USE
    #pragma omp parallel for schedule (static) if(n>OMP_LIMIT_COMPUTELIGHT)
  #endif
  for(int p=0;p<n;p++){
    const unsigned pnew=unsigned(p)+pini;
    const unsigned rp=listp[p];
    const unsigned pcopy=(rp&0x7FFFFFFF);
    //-Adjust position and cell of new particle. | Ajusta posicion y celda de nueva particula.
    PeriodicDuplicatePos(pnew,pcopy,(rp>=0x80000000),perinc.x,perinc.y,perinc.z,cellmax,pos,dcell);
    //-Copy the rest of the values. | Copia el resto de datos.
    idp[pnew]=idp[pcopy];
    code[pnew]=CODE_SetPeriodic(code[pcopy]);
    velrhop[pnew]=velrhop[pcopy];
    if(pospre)pospre[pnew]=pospre[pcopy];
    if(velrhoppre)velrhoppre[pnew]=velrhoppre[pcopy];
    if(spstau)spstau[pnew]=spstau[pcopy];
  }
}

//==============================================================================
/// Create periodic particles starting from a list of the particles to duplicate.
/// Assume that all the particles are valid.
/// This kernel works for single-cpu & multi-cpu because it uses domposmin.
///
/// Crea particulas periodicas a partir de una lista con las particulas a duplicar.
/// Se presupone que todas las particulas son validas.
/// Este kernel vale para single-cpu y multi-cpu porque usa domposmin. 
//==============================================================================
void JSphCpuSingle::PeriodicDuplicateNormals(unsigned np,unsigned pini,tuint3 cellmax
  ,tdouble3 perinc,const unsigned *listp,tfloat3 *normals,tfloat3 *motionvel)const
{
  const int n=int(np);
  #ifdef OMP_USE
    #pragma omp parallel for schedule (static) if(n>OMP_LIMIT_COMPUTELIGHT)
  #endif
  for(int p=0;p<n;p++){
    const unsigned pnew=unsigned(p)+pini;
    const unsigned rp=listp[p];
    const unsigned pcopy=(rp&0x7FFFFFFF);
    normals[pnew]=normals[pcopy];
    if(motionvel)motionvel[pnew]=motionvel[pcopy];
  }
}

//==============================================================================
/// Create duplicate particles for periodic conditions.
/// Create new periodic particles and mark the old ones to be ignored.
/// New periodic particles are created from Np of the beginning, first the NpbPer
/// of the boundry and then the NpfPer fluid ones. The Np of the those leaving contains also the
/// new periodic ones.
///
/// Crea particulas duplicadas de condiciones periodicas.
/// Crea nuevas particulas periodicas y marca las viejas para ignorarlas.
/// Las nuevas periodicas se situan a partir del Np de entrada, primero las NpbPer
/// de contorno y despues las NpfPer fluidas. El Np de salida contiene tambien las
/// nuevas periodicas.
//==============================================================================
void JSphCpuSingle::RunPeriodic(){
  Timersc->TmStart(TMC_SuPeriodic);
  //-Keep number of present periodic. | Guarda numero de periodicas actuales.
  NpfPerM1=NpfPer;
  NpbPerM1=NpbPer;
  //-Mark present periodic particles to ignore. | Marca periodicas actuales para ignorar.
  for(unsigned p=0;p<Np;p++){
    const typecode rcode=Codec[p];
    if(CODE_IsPeriodic(rcode))Codec[p]=CODE_SetOutIgnore(rcode);
  }
  //-Create new periodic particles. | Crea las nuevas periodicas.
  const unsigned npb0=Npb;
  const unsigned npf0=Np-Npb;
  const unsigned np0=Np;
  NpbPer=NpfPer=0;
  BoundChanged=true;
  for(unsigned ctype=0;ctype<2;ctype++){//-0:bound, 1:fluid+floating.
    //-Calculate range of particles to be examined (bound or fluid). | Calcula rango de particulas a examinar (bound o fluid).
    const unsigned pini=(ctype? npb0: 0);
    const unsigned num= (ctype? npf0: npb0);
    //-Search for periodic in each direction (X, Y, or Z). | Busca periodicas en cada eje (X, Y e Z).
    for(unsigned cper=0;cper<3;cper++)if((cper==0 && PeriX) || (cper==1 && PeriY) || (cper==2 && PeriZ)){
      tdouble3 perinc=(cper==0? PeriXinc: (cper==1? PeriYinc: PeriZinc));
      //-First search in the list of new periodic particles and then in the initial list of particles (this is needed for periodic particles in more than one direction).
      //-Primero busca en la lista de periodicas nuevas y despues en la lista inicial de particulas (necesario para periodicas en mas de un eje).
      for(unsigned cblock=0;cblock<2;cblock++){//-0:new periodic, 1:original particles. | 0:periodicas nuevas, 1:particulas originales
        const unsigned nper=(ctype? NpfPer: NpbPer); //-Number of new periodic particles of type to be processed. | Numero de periodicas nuevas del tipo a procesar.
        const unsigned pini2=(cblock? pini: Np-nper);
        const unsigned num2= (cblock? num:  nper);
        //-Repeat the search if the resulting memory available is insufficient and it had to be increased.
        //-Repite la busqueda si la memoria disponible resulto insuficiente y hubo que aumentarla.
        bool run=true;
        while(run && num2){
          //-Reserve memory to create list of periodic particles. | Reserva memoria para crear lista de particulas periodicas.
          unsigned* listp=ArraysCpu->ReserveUint();
          unsigned nmax=CpuParticlesSize-1; //-Maximmum number of particles that fit in the list. | Numero maximo de particulas que caben en la lista.
          //-Generate list of new periodic particles. | Genera lista de nuevas periodicas.
          if(Np>=0x80000000)Run_Exceptioon("The number of particles is too big.");//-Because the last bit is used to mark the direction in which a new periodic particle is created. | Porque el ultimo bit se usa para marcar el sentido en que se crea la nueva periodica.
          unsigned count=PeriodicMakeList(num2,pini2,Stable,nmax,perinc,Posc,Codec,listp);
          //-Redimension memory for particles if there is insufficient space and repeat the search process.
          //-Redimensiona memoria para particulas si no hay espacio suficiente y repite el proceso de busqueda.
          if(count>nmax || !CheckCpuParticlesSize(count+Np)){
            ArraysCpu->Free(listp); listp=NULL;
            Timersc->TmStop(TMC_SuPeriodic);
            ResizeParticlesSize(Np+count,PERIODIC_OVERMEMORYNP,false);
            Timersc->TmStart(TMC_SuPeriodic);
          }
          else{
            run=false;
            //-Create new duplicate periodic particles in the list
            //-Crea nuevas particulas periodicas duplicando las particulas de la lista.
            if(TStep==STEP_Verlet)PeriodicDuplicateVerlet(count,Np,DomCells,perinc,listp,Idpc,Codec,Dcellc,Posc,Velrhopc,SpsTauc,VelrhopM1c);
            if(TStep==STEP_Symplectic){
              if((PosPrec || VelrhopPrec) && (!PosPrec || !VelrhopPrec))Run_Exceptioon("Symplectic data is invalid.") ;
              PeriodicDuplicateSymplectic(count,Np,DomCells,perinc,listp,Idpc,Codec,Dcellc,Posc,Velrhopc,SpsTauc,PosPrec,VelrhopPrec);
            }
            if(UseNormals)PeriodicDuplicateNormals(count,Np,DomCells,perinc,listp,BoundNormalc,MotionVelc);

            //-Free the list and update the number of particles. | Libera lista y actualiza numero de particulas.
            ArraysCpu->Free(listp); listp=NULL;
            Np+=count;
            //-Update number of new periodic particles. | Actualiza numero de periodicas nuevas.
            if(!ctype)NpbPer+=count;
            else NpfPer+=count;
          }
        }
      }
    }
  }
  Timersc->TmStop(TMC_SuPeriodic);
}

//==============================================================================
/// Executes divide of particles in cells.
/// Ejecuta divide de particulas en celdas.
//==============================================================================
void JSphCpuSingle::RunCellDivide(bool updateperiodic){
  DivData=DivDataCpuNull();
  //-Creates new periodic particles and marks the old ones to be ignored.
  //-Crea nuevas particulas periodicas y marca las viejas para ignorarlas.
  if(updateperiodic && PeriActive)RunPeriodic();

  //-Initiates Divide.
  CellDivSingle->Divide(Npb,Np-Npb-NpbPer-NpfPer,NpbPer,NpfPer,BoundChanged
    ,Dcellc,Codec,Idpc,Posc,Timersc);
  DivData=CellDivSingle->GetCellDivData();

  //-Sorts particle data. | Ordena datos de particulas.
  Timersc->TmStart(TMC_NlSortData);
  CellDivSingle->SortArray(Idpc);
  CellDivSingle->SortArray(Codec);
  CellDivSingle->SortArray(Dcellc);
  CellDivSingle->SortArray(Posc);
  CellDivSingle->SortArray(Velrhopc);
  CellDivSingle->SortArray(ParDet);
  CellDivSingle->SortArray(NormalVec);
  CellDivSingle->SortArray(VelDivc);
  CellDivSingle->SortArray(viscoArray);
  CellDivSingle->SortArray(RhoOver);
  CellDivSingle->SortArray(CorrMatc);
  //-Sorts VEM Pressure and Acceleration
  if (VemAdded) { CellDivSingle->SortArray(PressVemc); CellDivSingle->SortArray(AceVemc); }
  //-Sorts HPDC Psi
  if (HpdcAdded) { CellDivSingle->SortArray(Psic); }
  //-Sorts VCS Pressure And Shifting Vector
  if (VcsAdded) { CellDivSingle->SortArray(PressVcsc); CellDivSingle->SortArray(ShiftVcsC); }
  if(TStep==STEP_Verlet){
    CellDivSingle->SortArray(VelrhopM1c);
  }
  else if(TStep==STEP_Symplectic && (PosPrec || VelrhopPrec)){//-In reality, this is only necessary in divide for corrector, not in predictor??? | En realidad solo es necesario en el divide del corrector, no en el predictor???
    if(!PosPrec || !VelrhopPrec)Run_Exceptioon("Symplectic data is invalid.") ;
    CellDivSingle->SortArray(PosPrec);
    CellDivSingle->SortArray(VelrhopPrec);
    if (HpdcAdded)CellDivSingle->SortArray(PsiPrec);
  }
  if(TVisco==VISCO_LaminarSPS)CellDivSingle->SortArray(SpsTauc);
  if(UseNormals){
    CellDivSingle->SortArray(BoundNormalc);
    if(MotionVelc)CellDivSingle->SortArray(MotionVelc);
  }

  //-Collect divide data. | Recupera datos del divide.
  Np=CellDivSingle->GetNpFinal();
  Npb=CellDivSingle->GetNpbFinal();
  NpbOk=Npb-CellDivSingle->GetNpbIgnore();

  //-Manages excluded particles fixed, moving and floating before aborting the execution.
  //if(CellDivSingle->GetNpbOut())AbortBoundOut();

  //-Collect position of floating particles. | Recupera posiciones de floatings.
  if(CaseNfloat)CalcRidp(PeriActive!=0,Np-Npb,Npb,CaseNpb,CaseNpb+CaseNfloat,Codec,Idpc,FtRidp);
  Timersc->TmStop(TMC_NlSortData);

  //-Control of excluded particles (only fluid because excluded boundary are checked before).
  //-Gestion de particulas excluidas (solo fluid porque las boundary excluidas se comprueban antes).
  Timersc->TmStart(TMC_NlOutCheck);
  unsigned npfout=CellDivSingle->GetNpfOut();
  //if(npfout){
  //  unsigned* idp=ArraysCpu->ReserveUint();
  //  tdouble3* pos=ArraysCpu->ReserveDouble3();
  //  tfloat3* vel=ArraysCpu->ReserveFloat3();
  //  float* rhop=ArraysCpu->ReserveFloat();
  //  typecode* code=ArraysCpu->ReserveTypeCode();
  //  unsigned* pardetction = ArraysCpu->ReserveUint();
  //  float* sourcedi = ArraysCpu->ReserveFloat();
  //  float* sourcedf = ArraysCpu->ReserveFloat();
  //  float* rhoover = ArraysCpu->ReserveFloat();
  //  float* veldiv = ArraysCpu->ReserveFloat();
  //  float* pressvcs = ArraysCpu->ReserveFloat();
  //  tfloat3 *parnormal = ArraysCpu->ReserveFloat3();
  //  tdouble3* shiftvcs = ArraysCpu->ReserveDouble3();
  //  unsigned num=GetParticlesData(npfout,Np,false,idp,pos,vel,rhop, pardetction,veldiv,rhoover,sourcedi,sourcedf,pressvcs,parnormal,shiftvcs,code);
  //  AddParticlesOut(npfout,idp,pos,vel,rhop,code);
  //  ArraysCpu->Free(idp);
  //  ArraysCpu->Free(pos);
  //  ArraysCpu->Free(vel);
  //  ArraysCpu->Free(rhop);
  //  ArraysCpu->Free(code);
  //  ArraysCpu->Free(pardetction);
  //  ArraysCpu->Free(sourcedi);
  //  ArraysCpu->Free(sourcedf);
  //  //ArraysCpu->Free(rhobar);
  //  ArraysCpu->Free(veldiv);
  //  ArraysCpu->Free(pressvcs);
  //  ArraysCpu->Free(parnormal);
  //  ArraysCpu->Free(shiftvcs);
  //}
  Timersc->TmStop(TMC_NlOutCheck);
  BoundChanged=false;
}

//==============================================================================
/// Manages excluded particles fixed, moving and floating before aborting the execution.
/// Gestiona particulas excluidas fixed, moving y floating antes de abortar la ejecucion.
//==============================================================================
void JSphCpuSingle::AbortBoundOut(){
  const unsigned nboundout=CellDivSingle->GetNpbOut();
  //-Get data of excluded boundary particles.
  unsigned* idp=ArraysCpu->ReserveUint();
  tdouble3* pos=ArraysCpu->ReserveDouble3();
  tfloat3* vel=ArraysCpu->ReserveFloat3();
  float* rhop=ArraysCpu->ReserveFloat();
  typecode* code=ArraysCpu->ReserveTypeCode();
  unsigned* pardetction = ArraysCpu->ReserveUint();
  float* rhoover = ArraysCpu->ReserveFloat();
  float* veldiv = ArraysCpu->ReserveFloat();
  tfloat3* parnormal = ArraysCpu->ReserveFloat3();
  GetParticlesData(nboundout,Np,false,idp,pos,vel,rhop,pardetction, veldiv,rhoover,parnormal,code);
  //-Shows excluded particles information and aborts execution.
  JSph::AbortBoundOut(Log,nboundout,idp,pos,vel,rhop,code);
}

//==============================================================================
/// Interaction to calculate forces.
/// Interaccion para el calculo de fuerzas.
//==============================================================================
void JSphCpuSingle::Interaction_Forces(TpInterStep interstep){
  //-Since dummy particles are adopted here, MDBC is inactivated
  //if(TBoundary==BC_MDBC && (MdbcCorrector || interstep!=INTERSTEP_SymCorrector))MdbcBoundCorrection(); //-Boundary correction for mDBC.
  if (interstep != INTERSTEP_SymCorrector) Preinteraction();//-renormalized(correct) matrix, normal vector for particles, and particle category 
  InterStep=interstep;
  PreInteraction_Forces();
  tfloat3 *dengradcorr=NULL;

  Timersc->TmStart(TMC_CfForces);
  //-Interaction of Fluid-Fluid/Bound & Bound-Fluid (forces and DEM). | Interaccion Fluid-Fluid/Bound & Bound-Fluid (forces and DEM).
  const stinterparmsc parms=StInterparmsc(Np,Npb,NpbOk
    ,DivData,Dcellc
    ,Posc,Velrhopc,Idpc,Codec,Pressc,dengradcorr
    ,Arc,Acec,Deltac
    ,ShiftingMode,ShiftPosfsc
    ,SpsTauc,SpsGradvelc,CorrMatc,ParDet,NormalVec,Psic,APsi
  );
  StInterResultc res;
  res.viscdt=0;
  JSphCpu::Interaction_Forces_ct(parms,res);

  //-For 2-D simulations zero the 2nd component. | Para simulaciones 2D anula siempre la 2nd componente.
  if(Simulate2D){
    const int ini=int(Npb),fin=int(Np),npf=int(Np-Npb);
    #ifdef OMP_USE
      #pragma omp parallel for schedule (static) if(npf>OMP_LIMIT_COMPUTELIGHT)
    #endif
    for(int p=ini;p<fin;p++)Acec[p].y=0;
  }

  //-Add Delta-SPH correction to Arg[]. | Anhade correccion de Delta-SPH a Arg[].
  if(Deltac){
    const int ini=int(Npb),fin=int(Np),npf=int(Np-Npb);
    #ifdef OMP_USE
      #pragma omp parallel for schedule (static) if(npf>OMP_LIMIT_COMPUTELIGHT)
    #endif
    for(int p=ini;p<fin;p++)if(Deltac[p]!=FLT_MAX)Arc[p]+=Deltac[p];
  }

  //-Calculates maximum value of ViscDt.
  ViscDtMax=res.viscdt;
  //-Calculates maximum value of Ace (periodic particles are ignored).
  AceMax=ComputeAceMax(Np-Npb,Acec+Npb,Codec+Npb);

  Timersc->TmStop(TMC_CfForces);
}

//==============================================================================
/// Calculates extrapolated data on boundary particles from fluid domain for mDBC.
/// Calcula datos extrapolados en el contorno para mDBC.
//==============================================================================
void JSphCpuSingle::MdbcBoundCorrection(){
  Timersc->TmStart(TMC_CfPreForces);
  Interaction_MdbcCorrection(SlipMode,DivData,Posc,Codec,Idpc,BoundNormalc,MotionVelc,Velrhopc);
  Timersc->TmStop(TMC_CfPreForces);
}

void JSphCpuSingle::Preinteraction() {
    //Timersc->TmStart(TMC_CfPreForces);
    memset(NormalVec, 0, sizeof(tfloat3) * Np);
    memset(CorrMatc, 0, sizeof(tmatrix2d) * Np);
    memset(ParDet, 0, sizeof(unsigned) * Np);
    if(VemAdded){
        memset(PressVemc, 0, sizeof(float) * Np);
        memset(AceVemc, 0, sizeof(tdouble3) * Np);
    }
    if (VcsAdded) {
        memset(ShiftVcsC, 0, sizeof(tdouble3) * Np);
    }
    GetCorrectMatrixBound(Npb, 0, DivData, Dcellc, Posc, Velrhopc, CorrMatc);//-renormlized matrix for bound
    GetCorrectMatrix(Np - Npb, Npb, DivData, Dcellc, Posc, Velrhopc, CorrMatc);//-renormlized matrix for fluid
    FreeSurfaceDetction(Np - Npb, Npb, CorrMatc, ParDet, NormalVec, Velrhopc, Posc, DivData, Dcellc);//-particle category


    if(VemAdded||VcsAdded){
        const stvemvcsparmsc parmsvemvcs = StVEMVCSparm(VemAdded,VcsAdded,Np, Npb, NpbOk
            , DivData, Dcellc, Posc, Velrhopc,Pressc,ParDet,CorrMatc,PressVemc,AceVemc,PressVcsc,ShiftVcsC);
        //-Compute VEM pressure and VCS pressure
        RunVEMVCS_StepI(parmsvemvcs, SymplecticDtPre);
        //-Compute VEM acceleration and VCS shifting vector
        RunVEMVCS_StepII(parmsvemvcs, SymplecticDtPre);
    }
    //Timersc->TmStop(TMC_CfPreForces);
}
//==============================================================================
/// Returns maximum value of ace (modulus), periodic and inout particles must be ignored.
/// Devuelve el valor maximo de ace (modulo), se deben ignorar las particulas periodicas e inout.
//==============================================================================
double JSphCpuSingle::ComputeAceMax(unsigned np,const tfloat3* ace,const typecode *code)const{
  const bool check=(PeriActive!=0 || InOut!=NULL);
  if(check)return(ComputeAceMaxOmp<true >(Np-Npb,Acec+Npb,Codec+Npb));
  else     return(ComputeAceMaxOmp<false>(Np-Npb,Acec+Npb,Codec+Npb));
}

//==============================================================================
/// Returns maximum value of ace (modulus), periodic particles must be ignored.
/// Devuelve el valor maximo de ace (modulo), se deben ignorar las particulas periodicas.
//==============================================================================
template<bool checkcode> double JSphCpuSingle::ComputeAceMaxSeq(unsigned np
  ,const tfloat3* ace,const typecode *code)const
{
  float acemax=0;
  const int n=int(np);
  for(int p=0;p<n;p++){
    const typecode cod=(checkcode? code[p]: 0);
    const tfloat3 a=(!checkcode || (CODE_IsNormal(cod) && !CODE_IsFluidInout(cod))? ace[p]: TFloat3(0));
    const float a2=a.x*a.x+a.y*a.y+a.z*a.z;
    acemax=max(acemax,a2);
  }
  return(sqrt(double(acemax)));
}

//==============================================================================
/// Returns maximum value of ace (modulus) using OpenMP, periodic particles must be ignored.
/// Devuelve el valor maximo de ace (modulo) using OpenMP, se deben ignorar las particulas periodicas.
//==============================================================================
template<bool checkcode> double JSphCpuSingle::ComputeAceMaxOmp(unsigned np
  ,const tfloat3* ace,const typecode *code)const
{
  double acemax=0;
  #ifdef OMP_USE
    if(np>OMP_LIMIT_COMPUTELIGHT){
      const int n=int(np);
      if(n<0)Run_Exceptioon("Number of values is too big.");
      float amax=0;
      #pragma omp parallel 
      {
        float amax2=0;
        #pragma omp for nowait
        for(int p=0;p<n;++p){
          const typecode cod=(checkcode? code[p]: 0);
          const tfloat3 a=(!checkcode || (CODE_IsNormal(cod) && !CODE_IsFluidInout(cod))? ace[p]: TFloat3(0));
          const float a2=a.x*a.x+a.y*a.y+a.z*a.z;
          if(amax2<a2)amax2=a2;
        }
        #pragma omp critical 
        {
          if(amax<amax2)amax=amax2;
        }
      }
      //-Saves result.
      acemax=sqrt(double(amax));
    }
    else if(np)acemax=ComputeAceMaxSeq<checkcode>(np,ace,code);
  #else
    if(np)acemax=ComputeAceMaxSeq<checkcode>(np,ace,code);
  #endif
  return(acemax);
}

//<vs_ddramp_ini>
//==============================================================================
/// Applies initial DDT ramp.
//==============================================================================
void JSphCpuSingle::RunInitialDDTRamp(){
  if(TimeStep<DDTRamp.x){
    if((Nstep%10)==0){//-DDTkh value is updated every 10 calculation steps.
      if(TimeStep<=DDTRamp.y)DDTkh=KernelSize * float(DDTRamp.z);
      else{
        const double tt=TimeStep-DDTRamp.y;
        const double tr=DDTRamp.x-DDTRamp.y;
        DDTkh=KernelSize * float(((tr-tt)/tr)*(DDTRamp.z-DDTValue)+DDTValue);
      }
    }
  }
  else{
    if(DDTkh!=DDTkhCte)CSP.ddtkh=DDTkh=DDTkhCte;
    DDTRamp.x=0;
  }
}//<vs_ddramp_end>

//==============================================================================
/// Perform interactions and updates of particles according to forces 
/// calculated in the interaction using Verlet.
///
/// Realiza interaccion y actualizacion de particulas segun las fuerzas 
/// calculadas en la interaccion usando Verlet.
//==============================================================================
double JSphCpuSingle::ComputeStep_Ver(){
  Interaction_Forces(INTERSTEP_Verlet);    //-Interaction.
  const double dt=DtVariable(true);        //-Calculate new dt.
  if(CaseNmoving)CalcMotion(dt);           //-Calculate motion for moving bodies.
  DemDtForce=dt;                           //(DEM)
  if(Shifting)RunShifting(dt);             //-Shifting.
  ComputeVerlet(dt);                       //-Update particles using Verlet.
  if(CaseNfloat)RunFloating(dt,false);     //-Control of floating bodies.
  PosInteraction_Forces();                 //-Free memory used for interaction.
  if(Damping)RunDamping(dt,Np,Npb,Posc,Codec,Velrhopc); //-Applies Damping.
  if(RelaxZones)RunRelaxZone(dt);          //-Generate waves using RZ.
  return(dt);
}

//==============================================================================
/// Perform interactions and updates of particles according to forces 
/// calculated in the interaction using Symplectic.
///
/// Realiza interaccion y actualizacion de particulas segun las fuerzas 
/// calculadas en la interaccion usando Symplectic.
//==============================================================================
double JSphCpuSingle::ComputeStep_Sym(){
  const double dt=SymplecticDtPre;
  if(CaseNmoving)CalcMotion(dt);               //-Calculate motion for moving bodies.
  //-Predictor
  //-----------
  DemDtForce=dt*0.5f;                          //(DEM)
  Cal_VelDiv_Rho_Rho0();                       //-Get velocity divergence and rhoi/rho0
  Interaction_Forces(INTERSTEP_SymPredictor);  //-Interaction.
  //if (VemAdded)RunVem(dt);                     //-Calculate VEM pressure
  //if (VcsAdded)RunVcs(dt);                     //-Calculate VCS pressure and shifting value
  const double ddt_p=DtVariable(false);        //-Calculate dt of predictor step.
  if(Shifting)RunShifting(dt*.5);              //-Shifting.
  ComputeSymplecticPre(dt);                    //-Apply Symplectic-Predictor to particles (periodic particles become invalid).
  if(CaseNfloat)RunFloating(dt*.5,true);       //-Control of floating bodies.
  PosInteraction_Forces();                     //-Free memory used for interaction.
  //-Corrector
  //-----------
  DemDtForce=dt;                               //(DEM)
  RunCellDivide(true);
  Interaction_Forces(INTERSTEP_SymCorrector);  //-Interaction.
  //if (VemAdded)VemAceAdded();                  //-Calculate Ace from VEM Pressure Interaction
  const double ddt_c=DtVariable(true);         //-Calculate dt of corrector step.
  if(Shifting)RunShifting(dt);                 //-Shifting.
  ComputeSymplecticCorr(dt);                   //-Apply Symplectic-Corrector to particles (periodic particles become invalid).
  if(CaseNfloat)RunFloating(dt,false);         //-Control of floating bodies.
  PosInteraction_Forces();                     //-Free memory used for interaction.
  if(Damping)RunDamping(dt,Np,Npb,Posc,Codec,Velrhopc); //-Applies Damping.
  if(RelaxZones)RunRelaxZone(dt);              //-Generate waves using RZ.
  SymplecticDtPre=min(ddt_p,ddt_c);            //-Calculate dt for next ComputeStep.
  return(dt);
}

//==============================================================================
/// Calculate distance between floating particles & centre according to periodic conditions.
/// Calcula distancia entre pariculas floatin y centro segun condiciones periodicas.
//==============================================================================
tfloat3 JSphCpuSingle::FtPeriodicDist(const tdouble3 &pos,const tdouble3 &center,float radius)const{
  tdouble3 distd=(pos-center);
  if(PeriX && fabs(distd.x)>radius){
    if(distd.x>0)distd=distd+PeriXinc;
    else distd=distd-PeriXinc;
  }
  if(PeriY && fabs(distd.y)>radius){
    if(distd.y>0)distd=distd+PeriYinc;
    else distd=distd-PeriYinc;
  }
  if(PeriZ && fabs(distd.z)>radius){
    if(distd.z>0)distd=distd+PeriZinc;
    else distd=distd-PeriZinc;
  }
  return(ToTFloat3(distd));
}

//==============================================================================
/// Calculate summation of linear and angular forces starting from acceleration of particles.
/// Calcula suma de fuerzas lineal y angular a partir de la aceleracion de las particulas.
//==============================================================================
void JSphCpuSingle::FtCalcForcesSum(unsigned cf,tfloat3 &face,tfloat3 &fomegaace)const{
  const StFloatingData &fobj=FtObjs[cf];
  const unsigned fpini=fobj.begin-CaseNpb;
  const unsigned fpfin=fpini+fobj.count;
  const float fradius=fobj.radius;
  const tdouble3 fcenter=fobj.center;
  const float fmassp=fobj.massp;

  //-Computes sumation of forces starting from acceleration of particles.
  face=TFloat3(0);
  fomegaace=TFloat3(0);
  //-Calculate summation: face, fomegaace. | Calcula sumatorios: face, fomegaace.
  for(unsigned fp=fpini;fp<fpfin;fp++){
    int p=FtRidp[fp];
    const tfloat3 force=Acec[p]*fmassp;
    face=face+force;
    const tfloat3 dist=(PeriActive? FtPeriodicDist(Posc[p],fcenter,fradius): ToTFloat3(Posc[p]-fcenter)); 
    fomegaace.x+= force.z*dist.y - force.y*dist.z;
    fomegaace.y+= force.x*dist.z - force.z*dist.x;
    fomegaace.z+= force.y*dist.x - force.x*dist.y;
  }
}

//==============================================================================
/// Computes final acceleration from particles and from external forces to ftoforces[].
/// Calcula aceleracion final a parti de particulas y de fuerzas externas en ftoforces[].
//==============================================================================
void JSphCpuSingle::FtCalcForces(StFtoForces *ftoforces)const{
  const int ftcount=int(FtCount);
  #ifdef OMP_USE
    #pragma omp parallel for schedule (guided)
  #endif
  for(int cf=0;cf<ftcount;cf++){
    const StFloatingData fobj=FtObjs[cf];
    const float fmass=fobj.mass;
    const tfloat3 fang=fobj.angles;
    tmatrix3f inert=fobj.inertiaini;

    //-Compute a cumulative rotation matrix.
    const tmatrix3f frot=fmath::RotMatrix3x3(fang);
    //-Compute the inertia tensor by rotating the initial tensor to the curent orientation I=(R*I_0)*R^T.
    inert=fmath::MulMatrix3x3(fmath::MulMatrix3x3(frot,inert),fmath::TrasMatrix3x3(frot));
    //-Calculates the inverse of the inertia matrix to compute the I^-1 * L= W
    const tmatrix3f invinert=fmath::InverseMatrix3x3(inert);

    //-Compute summation of linear and angular forces starting from acceleration of particles.
    tfloat3 face,fomegaace;
    FtCalcForcesSum(cf,face,fomegaace);
    //-Adds inital external forces from ForcePoints, Moorings and external files.
    face=face+ftoforces[cf].face;
    fomegaace=fomegaace+ftoforces[cf].fomegaace;

    //-Calculate omega starting from fomegaace & invinert. | Calcula omega a partir de fomegaace y invinert.
    {
      tfloat3 omegaace;
      omegaace.x=(fomegaace.x*invinert.a11+fomegaace.y*invinert.a12+fomegaace.z*invinert.a13);
      omegaace.y=(fomegaace.x*invinert.a21+fomegaace.y*invinert.a22+fomegaace.z*invinert.a23);
      omegaace.z=(fomegaace.x*invinert.a31+fomegaace.y*invinert.a32+fomegaace.z*invinert.a33);
      fomegaace=omegaace;
    }
    //-Add gravity force and divide by mass. | Suma fuerza de gravedad y divide por la masa.
    face.x=(face.x + fmass*Gravity.x) / fmass;
    face.y=(face.y + fmass*Gravity.y) / fmass;
    face.z=(face.z + fmass*Gravity.z) / fmass;
    //-Keep result in ftoforces[]. | Guarda resultados en ftoforces[].
    ftoforces[cf].face=face; //-Saves acceleration (forces/fmass);
    ftoforces[cf].fomegaace=fomegaace;
  }
}

//==============================================================================
/// Calculate data to update floatings.
/// Calcula datos para actualizar floatings.
//==============================================================================
void JSphCpuSingle::FtCalcForcesRes(double dt,const StFtoForces *ftoforces,StFtoForcesRes *ftoforcesres)const{
  for(unsigned cf=0;cf<FtCount;cf++){
    //-Get Floating object values. | Obtiene datos de floating.
    const StFloatingData fobj=FtObjs[cf];
    //-Compute fomega. | Calculo de fomega.
    tfloat3 fomega=fobj.fomega;
    {
      const tfloat3 omegaace=ftoforces[cf].fomegaace;
      fomega.x=float(dt*omegaace.x+fomega.x);
      fomega.y=float(dt*omegaace.y+fomega.y);
      fomega.z=float(dt*omegaace.z+fomega.z);
    }
    tfloat3 fvel=fobj.fvel;
    //if(!cf)printf("--->fvel  f%d(%f,%f,%f)\n",cf,fvel.x,fvel.y,fvel.z);

    //-Zero components for 2-D simulation. | Anula componentes para 2D.
    tfloat3 face=ftoforces[cf].face;
    if(Simulate2D){ face.y=0; fomega.x=0; fomega.z=0; fvel.y=0; }
    //-Compute fcenter. | Calculo de fcenter.
    tdouble3 fcenter=fobj.center;
    fcenter.x+=dt*fvel.x;
    fcenter.y+=dt*fvel.y;
    fcenter.z+=dt*fvel.z;
    //-Compute fvel. | Calculo de fvel.
    fvel.x=float(dt*face.x+fvel.x);
    fvel.y=float(dt*face.y+fvel.y);
    fvel.z=float(dt*face.z+fvel.z);
    //-Store data to update floating. | Guarda datos para actualizar floatings.
    FtoForcesRes[cf].fomegares=fomega;
    FtoForcesRes[cf].fvelres=fvel;
    FtoForcesRes[cf].fcenterres=fcenter;
  }
}

//==============================================================================
/// Applies motion constraints.
/// Aplica restricciones de movimiento.
//==============================================================================
void JSphCpuSingle::FtApplyConstraints(StFtoForces *ftoforces,StFtoForcesRes *ftoforcesres)const{
  for(unsigned cf=0;cf<FtCount;cf++){
    const StFloatingData fobj=FtObjs[cf];
    if(fobj.constraints!=FTCON_Free){
      ApplyConstraints(fobj.constraints,ftoforces[cf].face,ftoforces[cf].fomegaace);
      ApplyConstraints(fobj.constraints,ftoforcesres[cf].fvelres,ftoforcesres[cf].fomegares);
    }
  }
}

//==============================================================================
/// Applies imposed velocity.
/// Aplica velocidad predefinida.
//==============================================================================
void JSphCpuSingle::FtApplyImposedVel(StFtoForcesRes *ftoforcesres)const{
  for(unsigned cf=0;cf<FtCount;cf++)if(!FtObjs[cf].usechrono && (FtLinearVel[cf]!=NULL || FtAngularVel[cf]!=NULL)){
    if(FtLinearVel[cf]!=NULL){
      const tfloat3 v=FtLinearVel[cf]->GetValue3f(TimeStep);
      if(v.x!=FLT_MAX)FtoForcesRes[cf].fvelres.x=v.x;
      if(v.y!=FLT_MAX)FtoForcesRes[cf].fvelres.y=v.y;
      if(v.z!=FLT_MAX)FtoForcesRes[cf].fvelres.z=v.z;
      //tfloat3 a=FtoForcesRes[cf].fvelres;
      //Log->Printf("%d> t:%f v(%f,%f,%f)",Nstep,TimeStep,a.x,a.y,a.z);
    }
    if(FtAngularVel[cf]!=NULL){
      const tfloat3 v=FtAngularVel[cf]->GetValue3f(TimeStep);
      if(v.x!=FLT_MAX)FtoForcesRes[cf].fomegares.x=v.x;
      if(v.y!=FLT_MAX)FtoForcesRes[cf].fomegares.y=v.y;
      if(v.z!=FLT_MAX)FtoForcesRes[cf].fomegares.z=v.z;
    }
  }
}

//==============================================================================
/// Process floating objects
/// Procesa floating objects.
//==============================================================================
void JSphCpuSingle::RunFloating(double dt,bool predictor){
  Timersc->TmStart(TMC_SuFloating);
  if(TimeStep>=FtPause){//-Operator >= is used because when FtPause=0 in symplectic-predictor, code would not enter here. | Se usa >= pq si FtPause es cero en symplectic-predictor no entraria.
    //-Initialises forces of floatings.
    memset(FtoForces,0,sizeof(StFtoForces)*FtCount); 

    //-Adds external forces (ForcePoints, Moorings, external file) to FtoForces[].
    if(ForcePoints!=NULL || FtLinearForce!=NULL){
      //-Loads sum of linear and angular forces from ForcePoints and Moorings.
      if(ForcePoints)ForcePoints->GetFtForcesSum(FtoForces);
      //-Adds the external forces.
      if(FtLinearForce!=NULL){
        for(unsigned cf=0;cf<FtCount;cf++){
          FtoForces[cf].face     =FtoForces[cf].face     +GetFtExternalForceLin(cf,TimeStep);
          FtoForces[cf].fomegaace=FtoForces[cf].fomegaace+GetFtExternalForceAng(cf,TimeStep);
        }
      }
    }

    //-Computes final acceleration from particles and from external forces in FtoForces[].
    FtCalcForces(FtoForces);

    //-Calculate data to update floatings. | Calcula datos para actualizar floatings.
    FtCalcForcesRes(dt,FtoForces,FtoForcesRes);
    //-Applies imposed velocity.
    if(FtLinearVel!=NULL)FtApplyImposedVel(FtoForcesRes);
    //-Applies motion constraints.
    if(FtConstraints)FtApplyConstraints(FtoForces,FtoForcesRes);

    //-Saves face and fomegace for debug.
    if(SaveFtAce)SaveFtAceFun(dt,predictor,FtoForces);

    //-Run floating with Chrono library.
    if(ChronoObjects){
      Timersc->TmStop(TMC_SuFloating);
      Timersc->TmStart(TMC_SuChrono);
      //-Export data / Exporta datos.
      for(unsigned cf=0;cf<FtCount;cf++)if(FtObjs[cf].usechrono){
        ChronoObjects->SetFtData(FtObjs[cf].mkbound,FtoForces[cf].face,FtoForces[cf].fomegaace);
      }
      //-Applies the external velocities to each floating body of Chrono.
      if(FtLinearVel!=NULL)ChronoFtApplyImposedVel();
      //-Calculate data using Chrono / Calcula datos usando Chrono.
      ChronoObjects->RunChrono(Nstep,TimeStep,dt,predictor);
      //-Load calculated data by Chrono / Carga datos calculados por Chrono.
      for(unsigned cf=0;cf<FtCount;cf++)if(FtObjs[cf].usechrono)ChronoObjects->GetFtData(FtObjs[cf].mkbound,FtoForcesRes[cf].fcenterres,FtoForcesRes[cf].fvelres,FtoForcesRes[cf].fomegares);
      Timersc->TmStop(TMC_SuChrono);
      Timersc->TmStart(TMC_SuFloating);
    }

    //-Apply movement around floating objects. | Aplica movimiento sobre floatings.
    const int ftcount=int(FtCount);
    #ifdef OMP_USE
      #pragma omp parallel for schedule (guided)
    #endif
    for(int cf=0;cf<ftcount;cf++){
      //-Get Floating object values.
      const StFloatingData fobj=FtObjs[cf];
      const tfloat3 fomega=FtoForcesRes[cf].fomegares;
      const tfloat3 fvel=FtoForcesRes[cf].fvelres;
      const tdouble3 fcenter=FtoForcesRes[cf].fcenterres;
      //-Updates floating particles.
      const float fradius=fobj.radius;
      const unsigned fpini=fobj.begin-CaseNpb;
      const unsigned fpfin=fpini+fobj.count;
      for(unsigned fp=fpini;fp<fpfin;fp++){
        const int p=FtRidp[fp];
        if(p!=UINT_MAX){
          tfloat4 *velrhop=Velrhopc+p;
          //-Compute and record position displacement. | Calcula y graba desplazamiento de posicion.
          const double dx=dt*double(velrhop->x);
          const double dy=dt*double(velrhop->y);
          const double dz=dt*double(velrhop->z);
          UpdatePos(Posc[p],dx,dy,dz,false,p,Posc,Dcellc,Codec);
          //-Compute and record new velocity. | Calcula y graba nueva velocidad.
          tfloat3 dist=(PeriActive? FtPeriodicDist(Posc[p],fcenter,fradius): ToTFloat3(Posc[p]-fcenter)); 
          velrhop->x=fvel.x+(fomega.y*dist.z-fomega.z*dist.y);
          velrhop->y=fvel.y+(fomega.z*dist.x-fomega.x*dist.z);
          velrhop->z=fvel.z+(fomega.x*dist.y-fomega.y*dist.x);
        }
      }

      //-Stores floating data.
      if(!predictor){
        FtObjs[cf].center=(PeriActive? UpdatePeriodicPos(fcenter): fcenter);
        FtObjs[cf].angles=ToTFloat3(ToTDouble3(FtObjs[cf].angles)+ToTDouble3(fomega)*dt);
        FtObjs[cf].facelin=(fvel  -FtObjs[cf].fvel  )/float(dt);
        FtObjs[cf].faceang=(fomega-FtObjs[cf].fomega)/float(dt);
        FtObjs[cf].fvel=fvel;
        FtObjs[cf].fomega=fomega;
        //-Updates floating normals for mDBC.
        if(UseNormalsFt){
          const tdouble3 dang=ToTDouble3(FtObjs[cf].angles-fobj.angles)*TODEG;
          const tdouble3 cen=FtObjs[cf].center;
          JMatrix4d mat;
          mat.Move(cen);
          mat.Rotate(dang);
          mat.Move(fobj.center*-1);
          for(unsigned fp=fpini;fp<fpfin;fp++){
            const int p=FtRidp[fp];
            if(p!=UINT_MAX)BoundNormalc[p]=ToTFloat3(mat.MulNormal(ToTDouble3(BoundNormalc[p])));
          }
        }
        //<vs_ftmottionsv_ini>
        if(FtMotSave && FtMotSave->CheckTime(TimeStep+dt))
          FtMotSave->SaveFtDataCpu(TimeStep+dt,Nstep+1,FtObjs,Np,Posc,FtRidp);
        //<vs_ftmottionsv_end>
      }
    }
  }
  Timersc->TmStop(TMC_SuFloating);
  //-Update data of points in FtForces and calculates motion data of affected floatings.
  if(!predictor && ForcePoints){
    Timersc->TmStart(TMC_SuMoorings);
    ForcePoints->UpdatePoints(TimeStep,dt,FtObjs);
    if(Moorings)Moorings->ComputeForces(Nstep,TimeStep,dt,ForcePoints);
    ForcePoints->ComputeForcesSum();
    Timersc->TmStop(TMC_SuMoorings);
  }
}

//==============================================================================
/// Runs calculations in configured gauges.
/// Ejecuta calculos en las posiciones de medida configuradas.
//==============================================================================
void JSphCpuSingle::RunGaugeSystem(double timestep,bool saveinput){
  //const bool svpart=(TimeStep>=TimePartNext);
  GaugeSystem->CalculeCpu(timestep,DivData,NpbOk,Npb,Np,Posc,Codec,Idpc,Velrhopc,saveinput);
}

//==============================================================================
/// Compute PIPS information of current particles.
/// Calcula datos de PIPS de particulas actuales.
//==============================================================================
void JSphCpuSingle::ComputePips(bool run){
  if(run || DsPips->CheckRun(Nstep)){
    TimerSim.Stop();
    const double timesim=TimerSim.GetElapsedTimeD()/1000.;
    DsPips->ComputeCpu(Nstep,TimeStep,timesim,CSP,OmpThreads,Np,Npb,NpbOk
      ,DivData,Dcellc,Posc);
  }
}

//==============================================================================
/// Initialises execution of simulation.
/// Inicia ejecucion de simulacion.
//==============================================================================
void JSphCpuSingle::Run(std::string appname,const JSphCfgRun *cfg,JLog2 *log){
  if(!cfg||!log)return;
  AppName=appname; Log=log; CfgRun=cfg;

  //-Configure timers.
  //-------------------
  Timersc->Config(cfg->SvTimers);
  Timersc->TmStart(TMC_Init);

  //-Load parameters and values of input. | Carga de parametros y datos de entrada.
  //--------------------------------------------------------------------------------
  LoadConfig(cfg);
  LoadCaseParticles();
  VisuConfig();
  ConfigDomain();
  ConfigRunMode();
  VisuParticleSummary();


// -----------square patch initial conditions----------
  //Gravity = TFloat3(0.0);
  //for (int p = Npb; p < Np; p++)
  //{
  //    const float rhoini = 1000;
  //    float pressp1 = 0;
  //    const float L = 1.0;
  //    const float OmegaSquare = 1.0;
  //    for (auto i = 0; i < 3; i++)
  //    {
  //        int m = 2 * i + 1;
  //        float mPi_L = m * PI / L;
  //        for (auto j = 0; j < 3; j++)
  //        {
  //            int n = 2 * j + 1;
  //            float nPi_L = n * PI / L;
  //            pressp1 += -32 * OmegaSquare * OmegaSquare / (m * n * PI * PI * (nPi_L * nPi_L + mPi_L * mPi_L))
  //                * sin(mPi_L * (Posc[p].x + L / 2)) * sin(nPi_L * (Posc[p].z + L / 2)) * rhoini;
  //        }
  //    }
  //    Velrhopc[p].w = fsph::ComputeRho(pressp1, CSP);
  //    Velrhopc[p].x = OmegaSquare * Posc[p].z;
  //    Velrhopc[p].y = 0;
  //    Velrhopc[p].z = -OmegaSquare * Posc[p].x;
  //    ek0 += 0.5 * MassFluid * (Velrhopc[p].x * Velrhopc[p].x + Velrhopc[p].z * Velrhopc[p].z);
  //}

   //-Two patches initial conditions.
  Gravity = TFloat3(0.0);
    for (int p = Npb; p < Np; p++)
  {
        //Velrhopc[p].w = 1.0;
        Velrhopc[p].x = 0.0;
        Velrhopc[p].z = Posc[p].z < 0 ? 1.0 : -1.0;
        ek0 += 0.5 * MassFluid * (Velrhopc[p].x * Velrhopc[p].x + Velrhopc[p].z * Velrhopc[p].z);
  }

  //-Initialisation of execution variables. | Inicializacion de variables de ejecucion.
  //------------------------------------------------------------------------------------
  InitRunCpu();
  RunGaugeSystem(TimeStep,true);
  if(InOut)InOutInit(TimeStepIni);
  FreePartsInit();
  UpdateMaxValues();
  PrintAllocMemory(GetAllocMemoryCpu());
  SaveData(); 
  Timersc->ResetTimes();
  Timersc->TmStop(TMC_Init);
  if(Log->WarningCount())Log->PrintWarningList("\n[WARNINGS]","");
  PartNstep=-1; Part++;
  Cal_Rho_Ref();//get rho_reference by kernel summation
  //-Main Loop.
  //------------
  JTimeControl tc("30,60,300,600");//-Shows information at 0.5, 1, 5 y 10 minutes (before first PART).
  bool partoutstop=false;
  TimerSim.Start();
  TimerPart.Start();
  Log->Print(string("\n[Initialising simulation (")+RunCode+")  "+fun::GetDateTime()+"]");
  if(DsPips)ComputePips(true);
  PrintHeadPart();
  while(TimeStep<TimeMax){
    InterStep=(TStep==STEP_Symplectic? INTERSTEP_SymPredictor: INTERSTEP_Verlet);
    if(ViscoTime)Visco=ViscoTime->GetVisco(float(TimeStep));
    if(DDTRamp.x)RunInitialDDTRamp(); //<vs_ddramp>
    double stepdt=ComputeStep();
    RunGaugeSystem(TimeStep+stepdt);
    if(CaseNmoving)RunMotion(stepdt);
    if(InOut)InOutComputeStep(stepdt);
    else RunCellDivide(true);
    TimeStep+=stepdt;
    LastDt=stepdt;
    partoutstop=(Np<NpMinimum || !Np);
    if(TimeStep>=TimePartNext || partoutstop){
      if(partoutstop){
        Log->PrintWarning("Particles OUT limit reached...");
        TimeMax=TimeStep;
      }
      SaveData();
      Part++;
      PartNstep=Nstep;
      TimeStepM1=TimeStep;
      TimePartNext=(SvAllSteps? TimeStep: OutputTime->GetNextTime(TimeStep));
      TimerPart.Start();
    }
    UpdateMaxValues();
    Nstep++;
    const bool laststep=(TimeStep>=TimeMax || (NstepsBreak && Nstep>=NstepsBreak));
    if(DsPips)ComputePips(laststep);
    if(Part<=PartIni+1 && tc.CheckTime())Log->Print(string("  ")+tc.GetInfoFinish((TimeStep-TimeStepIni)/(TimeMax-TimeStepIni)));
    if(NstepsBreak && Nstep>=NstepsBreak)break; //-For debugging.
  }
  TimerSim.Stop(); TimerTot.Stop();

  //-End of Simulation.
  //--------------------
  FinishRun(partoutstop);
}

//==============================================================================
/// Generates files with output data.
/// Genera los ficheros de salida de datos.
//==============================================================================
void JSphCpuSingle::SaveData(){
  const bool save=(SvData!=SDAT_None && SvData!=SDAT_Info);
  const unsigned npsave=Np-NpbPer-NpfPer; //-Subtracts the periodic particles if they exist. | Resta las periodicas si las hubiera.
  Timersc->TmStart(TMC_SuSavePart);
  //-Collect particle values in original order. | Recupera datos de particulas en orden original.
  unsigned *idp=NULL;
  tdouble3 *pos=NULL;
  tfloat3 *vel=NULL;
  float *rhop=NULL;
  unsigned* pardetction = NULL;
  float* veldiv = NULL;
  tfloat3* parnormal = NULL;
  float* rhoover = NULL;
  if(save){
    //-Assign memory and collect particle values. | Asigna memoria y recupera datos de las particulas.
    idp=ArraysCpu->ReserveUint();
    pos=ArraysCpu->ReserveDouble3();
    vel=ArraysCpu->ReserveFloat3();
    rhop=ArraysCpu->ReserveFloat();
    pardetction = ArraysCpu->ReserveUint();
    veldiv = ArraysCpu->ReserveFloat();
    parnormal = ArraysCpu->ReserveFloat3();
    rhoover = ArraysCpu->ReserveFloat();
    unsigned npnormal=GetParticlesData(Np,0,PeriActive!=0,idp,pos,vel,rhop, pardetction,veldiv,rhoover,parnormal,NULL);
    if(npnormal!=npsave)Run_Exceptioon("The number of particles is invalid.");
  }
  //-Gather additional information. | Reune informacion adicional.
  StInfoPartPlus infoplus;
  memset(&infoplus,0,sizeof(StInfoPartPlus));
  if(SvData&SDAT_Info){
    infoplus.nct=CellDivSingle->GetNct();
    infoplus.npbin=NpbOk;
    infoplus.npbout=Npb-NpbOk;
    infoplus.npf=Np-Npb;
    infoplus.npbper=NpbPer;
    infoplus.npfper=NpfPer;
    infoplus.newnp=(InOut? InOut->GetNewNpPart(): 0);
    infoplus.memorycpualloc=this->GetAllocMemoryCpu();
    infoplus.gpudata=false;
    TimerSim.Stop();
    infoplus.timesim=TimerSim.GetElapsedTimeD()/1000.;
  }
  //-Obtains current domain limits.
  const tdouble3 vdom[2]={CellDivSingle->GetDomainLimits(true),CellDivSingle->GetDomainLimits(false)};
  //-Stores particle data. | Graba datos de particulas.
  JDataArrays arrays;
  AddBasicArrays(arrays,npsave,pos,idp,vel,rhop, pardetction, veldiv,rhoover,parnormal);
  JSph::SaveData(npsave,arrays,1,vdom,&infoplus);
  //-Free auxiliary memory for particle data. | Libera memoria auxiliar para datos de particulas.
  ArraysCpu->Free(idp);
  ArraysCpu->Free(pos);
  ArraysCpu->Free(vel);
  ArraysCpu->Free(rhop);
  ArraysCpu->Free(pardetction);
  ArraysCpu->Free(veldiv);
  ArraysCpu->Free(parnormal);
  ArraysCpu->Free(rhoover);
  if(UseNormals && SvNormals)SaveVtkNormals("normals/Normals.vtk",Part,npsave,Npb,Posc,Idpc,BoundNormalc,1.f);
  //-Save extra data.
  if(SvExtraDataBi4)SaveExtraData();
  Timersc->TmStop(TMC_SuSavePart);
}

//==============================================================================
/// Displays and stores final summary of the execution.
/// Muestra y graba resumen final de ejecucion.
//==============================================================================
void JSphCpuSingle::SaveExtraData(){
  const bool svextra=(BoundNormalc!=NULL);
  if(svextra && SvExtraDataBi4->CheckSave(Part)){
    SvExtraDataBi4->InitPartData(Part,TimeStep,Nstep);
    //-Saves normals of mDBC.
    if(BoundNormalc){
      SvExtraDataBi4->AddNormals(UseNormalsFt,Np,Npb,Idpc,(PeriActive? Codec: NULL),BoundNormalc);
    }
    //-Saves file.
    SvExtraDataBi4->SavePartData();
  }
}

//==============================================================================
/// Displays and stores final summary of the execution.
/// Muestra y graba resumen final de ejecucion.
//==============================================================================
void JSphCpuSingle::FinishRun(bool stop){
  float tsim=TimerSim.GetElapsedTimeF()/1000.f,ttot=TimerTot.GetElapsedTimeF()/1000.f;
  JSph::ShowResume(stop,tsim,ttot,true,"");
  Log->Print(" ");
  string hinfo,dinfo;
  if(SvTimers){
    Timersc->ShowTimes("[CPU Timers]",Log);
    Timersc->GetTimersInfo(hinfo,dinfo);
  }
  if(SvRes)SaveRes(tsim,ttot,hinfo,dinfo);
  Log->PrintFilesList();
  Log->PrintWarningList();
  VisuRefs();
}

void JSphCpuSingle::Cal_VelDiv_Rho_Rho0() {
     memset(VelDivc, 0, sizeof(float) * Np);
     memset(RhoOver, 0, sizeof(float) * Np);
    const stinterparmsc parms = StInterparmsc(Np, Npb, NpbOk
        , DivData, Dcellc
        , Posc, Velrhopc, Idpc, Codec);
    JSphCpu::CalDiv(parms);
}

//- Calculate the reference density based on kernel summation
void JSphCpuSingle::Cal_Rho_Ref() {

    const stinterparmsc parms = StInterparmsc(Np, Npb, NpbOk
        , DivData, Dcellc
        , Posc, Velrhopc, Idpc, Codec);
    JSphCpu::GetRhoRef(parms);
}

void JSphCpuSingle::RunVcs(double dt) {
    memset(ShiftVcsC, 0, sizeof(tdouble3) * Np);
    // parameters for vcs packed into a structure
    const stvcsparmsc parmsvcs = StVcsparm(Np, Npb, NpbOk
        , DivData, Dcellc
        , Posc, Velrhopc, Pressc
        , ParDet , CorrMatc
        , PressVcsc, ShiftVcsC
    );
    JSphCpu::VcsStep(parmsvcs, dt);
}

void JSphCpuSingle::RunVem(double dt) {
    memset(PressVemc, 0, sizeof(float) * Np);
    memset(AceVemc, 0, sizeof(tdouble3) * Np);
    const stvemparmsc parmsvem = StVemparm(Np, Npb, NpbOk
        , DivData, Dcellc, Posc, Velrhopc, PressVemc);
    JSphCpu::RunVemPress_t1(parmsvem,dt);

    const stvemcorrparmsc parmsvem2 = StVemCorrparm(Np, Npb, NpbOk
        , DivData, Dcellc, Posc, Velrhopc, PressVemc, ParDet, AceVemc);
    //- From VEM pressure to AceVemc
    JSphCpu::VemPressInteraction_t1(parmsvem2);
}


void JSphCpuSingle::VemAceAdded() {

    const int ini = int(Npb), fin = int(Np), npf = int(Np - Npb);
    #ifdef OMP_USE
        #pragma omp parallel for schedule (static) if(npf>OMP_LIMIT_COMPUTELIGHT)
    #endif

    for (int p = ini; p < fin; p++) {
        float normace = TFloat3Norm(Acec[p]);
        float absacevem = TDouble3Norm(AceVemc[p]);
        float maxacevem = (VEMThreshold == 0) ? FLT_MAX : VEMThreshold * normace;
        if (absacevem > maxacevem) {
            Acec[p].x += maxacevem * AceVemc[p].x / absacevem;
            Acec[p].y += maxacevem * AceVemc[p].y / absacevem;
            Acec[p].z += maxacevem * AceVemc[p].z / absacevem;
        }
        else {
            Acec[p].x += AceVemc[p].x;
            Acec[p].y += AceVemc[p].y;
            Acec[p].z += AceVemc[p].z;
        }
    }
}