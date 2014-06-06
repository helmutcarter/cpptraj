#include "Trajin_Ensemble.h"
#include "CpptrajStdio.h"
#ifdef MPI
# include "MpiRoutines.h"
#endif

// CONSTRUCTOR
Trajin_Ensemble::Trajin_Ensemble() :
  targetType_(ReplicaInfo::NONE),
  eio_(0),
  trajIsOpen_(false),
  badEnsemble_(false),
  ensembleSize_(0)
{}

// DESTRUCTOR
Trajin_Ensemble::~Trajin_Ensemble() {
  EndTraj();
  if (eio_ != 0) delete eio_;
}

int Trajin_Ensemble::SetupTrajRead(std::string const& tnameIn, ArgList& argIn, Topology *tparmIn)
{
  // Require a filename
  if (tnameIn.empty()) {
    mprinterr("Internal Error: Trajin_Single: No filename given.\n");
    return 1;
  }
  // Check and set associated parm file
  if ( SetTrajParm( tparmIn ) ) return 1;
  // Check that file can be opened. 
  //if (!fileExists(tnameIn)) return 1;
  // Detect file format
  TrajFormatType tformat;
  if ( (eio_ = DetectFormat( tnameIn, tformat )) == 0 ) {
    mprinterr("Error: Could not determine trajectory %s format.\n", tnameIn.c_str());
    return 1;
  }
  eio_->SetDebug( debug_ );
  // Set trajectory filename
  SetTrajFileName( tnameIn, true );
  mprintf("\tReading '%s' as %s\n", TrajFilename().full(), TrajectoryFile::FormatString(tformat));
  // Should have already determined if this is single ensemble suitable,
  // but better safe than sorry.
  // TODO: There must be a better way. Pass in TrajectoryIO?
  if (!eio_->CanProcessEnsemble()) {
    mprinterr("Error: Cannot process single file ensemble with '%s'\n", FormatString(tformat));
    return 1;
  }
  bool nosort = argIn.hasKey("nosort");
  // Process format-specific read args
  if (eio_->processReadArgs( argIn )) return 1;
  // Set up the format for reading and get the number of frames.
  if (SetupTrajIO( tnameIn, *eio_, argIn )) return 1;
  // Check how many frames will actually be read
  if (setupFrameInfo() == 0) return 1;
  // Check traj box info against parm box info
  Box parmBox = tparmIn->ParmBox();
  if (CheckBoxInfo(tparmIn->c_str(), parmBox, eio_->TrajBox())) return 1;
  tparmIn->SetBox( parmBox );
  ensembleSize_ = eio_->EnsembleSize();
  trajRepDimInfo_ = eio_->ReplicaDimensions();
  // If dimensions are present, assume search by indices, otherwise by temp.
  targetType_ = ReplicaInfo::NONE;
  if (trajRepDimInfo_.Ndims() > 0)
    targetType_ = ReplicaInfo::INDICES;
  else if (eio_->HasT())
    targetType_ = ReplicaInfo::TEMP;
  else if (!nosort) {
    mprinterr("Error: Ensemble trajectory does not have indices or temperature.\n");
    return 1;
  }
  return 0;
}

int Trajin_Ensemble::BeginTraj(bool showProgress) {
  // Open the trajectory
  if (eio_->openTrajin()) {
    mprinterr("Error: Could not open %s\n",TrajFilename().base());
    return 1;
  }
  // Set progress bar, start and offset.
  PrepareForRead( showProgress );
  trajIsOpen_ = true;
  return 0;
}

void Trajin_Ensemble::EndTraj() {
  if (trajIsOpen_) {
    eio_->closeTraj();
    trajIsOpen_ = false;
  }
}

bool Trajin_Ensemble::HasVelocity() const {
  if (eio_ != 0)
    return eio_->HasV();
  else
    return false;
}

void Trajin_Ensemble::PrintInfo(int showExtended) const {
  mprintf("'%s' (REMD ensemble size %i) ",TrajFilename().base(), ensembleSize_);
  eio_->Info();
  mprintf(", Parm %s",TrajParm()->c_str());
  if (eio_->HasBox()) mprintf(" (%s box)", eio_->TrajBox().TypeName());
  if (showExtended==1) PrintFrameInfo();
  if (debug_>0)
    mprintf(", %i atoms, Box %i",TrajParm()->Natom(),(int)eio_->HasBox());
}

// -----------------------------------------------------------------------------
// Trajin_Ensemble::EnsembleInfo()
void Trajin_Ensemble::EnsembleInfo() const {
  if (targetType_ == ReplicaInfo::TEMP) {
    mprintf("  Ensemble Temperature Map:\n");
    for (ReplicaMap<double>::const_iterator tmap = TemperatureMap_.begin();
                                            tmap != TemperatureMap_.end(); ++tmap)
      mprintf("\t%10.2f -> %i\n", tmap->first, tmap->second);
  } else if (targetType_ == ReplicaInfo::INDICES) {
    mprintf("  Ensemble Indices Map:\n");
    for (ReplicaMap<RemdIdxType>::const_iterator imap = IndicesMap_.begin();
                                                 imap != IndicesMap_.end(); ++imap)
    {
      mprintf("\t{");
      for (RemdIdxType::const_iterator idx = imap->first.begin();
                                       idx != imap->first.end(); ++idx)
        mprintf(" %i", *idx);
      mprintf(" } -> %i\n", imap->second);
    }
  }
}

// Trajin_Ensemble::EnsembleSetup()
int Trajin_Ensemble::EnsembleSetup( FrameArray& f_ensemble, FramePtrArray& f_sorted ) {
  // Allocate space to hold position of each incoming frame in replica space.
# ifdef MPI
  // Only two frames needed; one for reading, one for receiving.
  f_sorted.resize( 2 );
  f_ensemble.resize( 2 );
  // This array will let each thread know who has what frame.
  frameidx_.resize( ensembleSize_ ); // TODO: Get rid of, should do all in TrajIO class.
# else
  f_sorted.resize( ensembleSize_ );
  f_ensemble.resize( ensembleSize_ );
# endif 
  f_ensemble.SetupFrames( TrajParm()->Atoms(), HasVelocity(), trajRepDimInfo_.Ndims() );
  // Get a list of all temperatures/indices.
  TemperatureMap_.ClearMap();
  IndicesMap_.ClearMap();
  if (targetType_ == ReplicaInfo::TEMP || targetType_ == ReplicaInfo::INDICES )
  {
    if ( eio_->openTrajin() ) return 1;
    if ( eio_->readArray( Start(), f_ensemble ) ) return 1;
    eio_->closeTraj();
    if (targetType_ == ReplicaInfo::TEMP) {
      std::vector<double> all_temperatures( ensembleSize_ );
#     ifdef MPI
      // Consolidate temperatures
      if (parallel_allgather(f_ensemble[0].tAddress(), 1, PARA_DOUBLE, 
                             &all_temperatures[0], 1, PARA_DOUBLE))
      {
        rprinterr("Error: Gathering temperatures\n");
        return 1; // TODO: Better parallel error check
      }
#     else
      for (unsigned int i = 0; i != ensembleSize; i++)
        all_temperatures[i] = f_ensemble[i].Temperature();
#     endif
      if (TemperatureMap_.CreateMap( all_temperatures )) {
        rprinterr("Error: Ensemble: Duplicate temperature detected (%.2f) in ensemble %s\n"
                  "Error:   If this is an H-REMD ensemble try the 'nosort' keyword.\n",
                   TemperatureMap_.Duplicate(), TrajFilename().full());
        return 1;
      }
    } else if (targetType_ == ReplicaInfo::INDICES) {
      std::vector<RemdIdxType> indices( ensembleSize_ );
#     ifdef MPI
      // Consolidate replica indices
      std::vector<int> all_indices( ensembleSize_ * trajRepDimInfo_.Ndims() );
      if (parallel_allgather( f_ensemble[0].iAddress(), trajRepDimInfo_.Ndims(), PARA_INT,
                              &all_indices[0], trajRepDimInfo_.Ndims(), PARA_INT ))
      {
        rprinterr("Error: Gathering indices\n");
        return 1; // TODO: Better parallel error check
      }
      std::vector<int>::const_iterator idx_it = all_indices.begin();
      for (std::vector<RemdIdxType>::iterator it = indices.begin();
                                              it != indices.end();
                                            ++it, idx_it += trajRepDimInfo_.Ndims())
        it->assign(idx_it, idx_it + trajRepDimInfo_.Ndims());
#     else
      for (unsigned int i = 0; i != ensembleSize_; i++)
        indices[i] = f_ensemble[i].RemdIndices();
#     endif
      if (IndicesMap_.CreateMap( indices )) {
        rprinterr("Error: Ensemble: Duplicate indices detected in ensemble %s:",
                  TrajFilename().full());
        for (RemdIdxType::const_iterator idx = IndicesMap_.Duplicate().begin();
                                         idx != IndicesMap_.Duplicate().end(); ++idx)
          rprinterr(" %i", *idx);
        rprinterr("\n");
        return 1;
      }
    }
  }

  return 0;
}

// Trajin_Ensemble::GetNextEnsemble()
int Trajin_Ensemble::GetNextEnsemble(  FrameArray& f_ensemble, FramePtrArray& f_sorted ) {
  badEnsemble_ = false;
  // If the current frame is out of range, exit
  if (CheckFinished()) return 0;
  // Read in all replicas.
  if ( eio_->readArray( CurrentFrame(), f_ensemble ) ) return 0;
# ifdef MPI
  int ensembleFrameNum = 0;
  if (targetType_ != ReplicaInfo::NONE) {
    int my_idx;
    if (targetType_ == ReplicaInfo::TEMP)
      my_idx = TemperatureMap_.FindIndex( f_ensemble[0].Temperature() );
    else if (targetType_ == ReplicaInfo::INDICES)
      my_idx = IndicesMap_.FindIndex( f_ensemble[0].RemdIndices() );
    // TODO: Put this in Traj_NcEnsemble
    if (parallel_allgather( &my_idx, 1, PARA_INT, &frameidx_[0], 1, PARA_INT)) {
      rprinterr("Error: Gathering frame indices.\n");
      badEnsemble_ = true;
      return 1; // TODO: Better parallel error check
    }
    for (int i = 0; i != ensembleSize_; i++)
      if (frameidx_[i] == -1) {
        badEnsemble_ = true;
        break;
      }
    if (!badEnsemble_) {
      for (int sendrank = 0; sendrank != ensembleSize_; sendrank++) {
        int recvrank = frameidx_[sendrank];
        if (sendrank != recvrank) {
          if (sendrank == worldrank)
            f_ensemble[0].SendFrame( recvrank );
          else if (recvrank == worldrank) {
            f_ensemble[1].RecvFrame( sendrank );
            // Since a frame was received, indicate position 1 should be used
            ensembleFrameNum = 1;
          }
        }
        //else rprintf("SEND RANK == RECV RANK, NO COMM\n"); // DEBUG
      }
    }
  }
  f_sorted[0] = &f_ensemble[ensembleFrameNum];
# else
  if (targetType_ == ReplicaInfo::TEMP) {
    for (unsigned int i = 0; i != ensembleSize_; i++) {
      int fidx = TemperatureMap_.FindIndex( f_ensemble[i].Temperature() );
      if ( fidx == -1 )
        badEnsemble_ = true;
      else
        f_sorted[fidx] = &f_ensemble[i];
    }
  } else if (targetType_ == ReplicaInfo::INDICES) {
    for (unsigned int i = 0; i != ensembleSize_; i++) {
      int fidx = IndicesMap_.FindIndex( f_ensemble[i].RemdIndices() );
      if (fidx == -1 )
        badEnsemble_ = true;
      else
        f_sorted[fidx] = &f_ensemble[i];
    }
  }
# endif
  UpdateCounters();
  return 1;
}
