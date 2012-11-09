// Action_Dihedral
#include "Action_Dihedral.h"
#include "CpptrajStdio.h"
#include "Constants.h" // RADDEG
#include "TorsionRoutines.h"

// CONSTRUCTOR
Action_Dihedral::Action_Dihedral() :
  dih_(NULL),
  useMass_(false)
{ } 

void Action_Dihedral::Help() {
  mprintf("dihedral [<name>] <mask1> <mask2> <mask3> <mask4> [out filename] [mass]\n");
  mprintf("         [type {alpha|beta|gamma|delta|epsilon|zeta|chi|c2p|h1p|phi|psi|pchi}]\n");
}

// Action_Dihedral::init()
Action::RetType Action_Dihedral::Init(ArgList& actionArgs, TopologyList* PFL, FrameList* FL,
                          DataSetList* DSL, DataFileList* DFL, int debugIn)
{
  // Get keywords
  DataFile* outfile = DFL->AddDataFile( actionArgs.GetStringKey("out"), actionArgs );
  useMass_ = actionArgs.hasKey("mass");
  DataSet::scalarType stype = DataSet::UNDEFINED;
  std::string stypename = actionArgs.GetStringKey("type");
  if      ( stypename == "alpha"   ) stype = DataSet::ALPHA;
  else if ( stypename == "beta"    ) stype = DataSet::BETA;
  else if ( stypename == "gamma"   ) stype = DataSet::GAMMA;
  else if ( stypename == "delta"   ) stype = DataSet::DELTA;
  else if ( stypename == "epsilon" ) stype = DataSet::EPSILON;
  else if ( stypename == "zeta"    ) stype = DataSet::ZETA;
  else if ( stypename == "chi"     ) stype = DataSet::CHI;
  else if ( stypename == "c2p"     ) stype = DataSet::C2P;
  else if ( stypename == "h1p"     ) stype = DataSet::H1P;
  else if ( stypename == "phi"     ) stype = DataSet::PHI;
  else if ( stypename == "psi"     ) stype = DataSet::PSI;
  else if ( stypename == "pchi"    ) stype = DataSet::PCHI;

  // Get Masks
  std::string mask1 = actionArgs.GetMaskNext();
  std::string mask2 = actionArgs.GetMaskNext();
  std::string mask3 = actionArgs.GetMaskNext();
  std::string mask4 = actionArgs.GetMaskNext();
  if (mask1.empty() || mask2.empty() || mask3.empty() || mask4.empty()) {
    mprinterr("Error: dihedral: Requires 4 masks\n");
    return Action::ERR;
  }
  M1_.SetMaskString(mask1);
  M2_.SetMaskString(mask2);
  M3_.SetMaskString(mask3);
  M4_.SetMaskString(mask4);

  // Setup dataset
  dih_ = DSL->AddSet(DataSet::DOUBLE, actionArgs.GetStringNext(),"Dih");
  if (dih_==0) return Action::ERR;
  dih_->SetScalar( DataSet::M_TORSION, stype );
  // Add dataset to datafile list
  if (outfile != 0) outfile->AddSet( dih_ );

  mprintf("    DIHEDRAL: [%s]-[%s]-[%s]-[%s]\n", M1_.MaskString(), 
          M2_.MaskString(), M3_.MaskString(), M4_.MaskString());
  if (useMass_)
    mprintf("              Using center of mass of atoms in masks.\n");

  return Action::OK;
}

// Action_Dihedral::setup
Action::RetType Action_Dihedral::Setup(Topology* currentParm, Topology** parmAddress) {
  if (currentParm->SetupIntegerMask(M1_)) return Action::ERR;
  if (currentParm->SetupIntegerMask(M2_)) return Action::ERR;
  if (currentParm->SetupIntegerMask(M3_)) return Action::ERR;
  if (currentParm->SetupIntegerMask(M4_)) return Action::ERR;
  M1_.MaskInfo();
  M2_.MaskInfo();
  M3_.MaskInfo();
  M4_.MaskInfo();
  if ( M1_.None() || M2_.None() || M3_.None() || M4_.None() ) {
    mprintf("Warning: dihedral: One or more masks have no atoms.\n");
    return Action::ERR;
  }

  return Action::OK;  
}

// Action_Dihedral::action()
Action::RetType Action_Dihedral::DoAction(int frameNum, Frame* currentFrame, Frame** frameAddress) {
  Vec3 a1, a2, a3, a4;

  if (useMass_) {
    a1 = currentFrame->VCenterOfMass( M1_ );
    a2 = currentFrame->VCenterOfMass( M2_ );
    a3 = currentFrame->VCenterOfMass( M3_ );
    a4 = currentFrame->VCenterOfMass( M4_ );
  } else {
    a1 = currentFrame->VGeometricCenter( M1_ );
    a2 = currentFrame->VGeometricCenter( M2_ );
    a3 = currentFrame->VGeometricCenter( M3_ );
    a4 = currentFrame->VGeometricCenter( M4_ );
  }
  double torsion = Torsion(a1.Dptr(), a2.Dptr(), a3.Dptr(), a4.Dptr());

  torsion *= RADDEG;

  dih_->Add(frameNum, &torsion);

  //fprintf(outfile,"%10i %10.4lf\n",frameNum,D);
  
  return Action::OK;
} 

