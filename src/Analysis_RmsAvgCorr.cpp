// Analysis_RmsAvgCorr
#include <cmath> // sqrt
#include "Analysis_RmsAvgCorr.h"
#include "CpptrajStdio.h"
#include "StringRoutines.h" // FIXME: Remove in cpptraj-dev
#include "ProgressBar.h"
#include "Trajin_Single.h" // For reading in reference
#ifdef _OPENMP
#  include "omp.h"
#endif

// CONSTRUCTOR
Analysis_RmsAvgCorr::Analysis_RmsAvgCorr() :
  coords_(0),
  Ct_(0),
  Csd_(0),
  maxwindow_(-1),
  lagOffset_(1),
  useMass_(false)
{ } 

void Analysis_RmsAvgCorr::Help() {
  mprintf("\t[crdset <crd set>] [<name>] [<mask>] [out <filename>] [mass]\n"
          "\t[stop <maxwindow>] [offset <offset>]\n"
          "\treference <ref file> parm <parmfile>\n"
          "\tCalculate the RMS average correlation, i.e. the average RMSD\n"
          "\tof structures which have been averaged over increasing numbers\n"
          "\tof frames.\n"
          "\t<crd set> can be created with the 'createcrd' command.\n");
}

Analysis::RetType Analysis_RmsAvgCorr::Setup(ArgList& analyzeArgs, DataSetList* datasetlist,
                            TopologyList* PFLin, DataFileList* DFLin, int debugIn)
{
  // Attempt to get coords dataset from datasetlist
  std::string setname = analyzeArgs.GetStringKey("crdset");
  coords_ = (DataSet_Coords*)datasetlist->FindCoordsSet( setname );
  if (coords_ == 0) {
    mprinterr("Error: rmsavgcorr: Could not locate COORDS set corresponding to %s\n",
              setname.c_str());
    return Analysis::ERR;
  }
  // Get Keywords
  lagOffset_ = analyzeArgs.getKeyInt("offset", 1);
  if (lagOffset_ < 1) lagOffset_ = 1;
  DataFile* outfile = DFLin->AddDataFile(analyzeArgs.GetStringKey("out"), analyzeArgs);
# ifdef _OPENMP
  if (analyzeArgs.hasKey("output")) {
    mprinterr("Error: 'output' keyword not supported in OpenMP version of rmsavgcorr.\n");
    return Analysis::ERR;
  }
# else
  separateName_ = analyzeArgs.GetStringKey("output");
# endif
  useMass_ = analyzeArgs.hasKey("mass");
  maxwindow_ = analyzeArgs.getKeyInt("stop",-1);
  // NOTE: REQUIRE A REFERENCE
  // FIXME: Should have access to previously loaded reference structures. Make data sets?
  std::string refFilename = analyzeArgs.GetStringKey("reference");
  if (refFilename.empty()) {
    mprinterr("Error: Must specify reference file.\n");
    return Analysis::ERR;
  }
  // Check for ref parm
  Topology* refParm = PFLin->GetParm(analyzeArgs);
  if (refParm == 0) {
    mprinterr("Error: Could not get ref parm.\n");
    return Analysis::ERR;
  }
  // Set up ref traj
  Trajin_Single traj;
  traj.SetDebug( debugIn );
  if ( traj.SetupTrajRead( refFilename, &analyzeArgs, refParm, false ) ) {
    mprinterr("Error: Could not set up reference '%s'\n", refFilename.c_str());
    return Analysis::ERR;
  }
  // Get Mask
  mask_.SetMaskString( analyzeArgs.GetMaskNext() );
  // LOAD REFERENCE
  // Check for reference mask
  std::string refMaskExpr = analyzeArgs.GetMaskNext();
  if (refMaskExpr.empty())
    refMaskExpr = mask_.MaskExpression();
  AtomMask refMask( refMaskExpr );
  if ( refParm->SetupIntegerMask( refMask ) ) return Analysis::ERR;
  refMask.MaskInfo();
  if ( refMask.None() ) {
    mprinterr("Error: No atoms in reference selected.\n");
    return Analysis::ERR;
  }
  // Read in reference structure if specified
  if ( traj.BeginTraj(false) ) {
    mprinterr("Error: could not open reference '%s'\n", traj.TrajFilename().full());
    return Analysis::ERR;
  }
  Frame inputFrame( refParm->Atoms() );
  traj.GetNextFrame( inputFrame );
  traj.EndTraj();
  refFrame_.SetupFrameFromMask( refMask, refParm->Atoms() );
  refFrame_.SetFrame( inputFrame, refMask );

  // Set up dataset to hold correlation 
  Ct_ = datasetlist->AddSet(DataSet::DOUBLE, analyzeArgs.GetStringNext(),"RACorr");
  if (Ct_==0) return Analysis::ERR;
  Csd_ = datasetlist->AddSetAspect(DataSet::DOUBLE, Ct_->Name(), "SD");
  if (Csd_==0) return Analysis::ERR;
  if (outfile != 0) {
    outfile->AddSet( Ct_ );
    outfile->AddSet( Csd_ );
    // FIXME: This will have to be changed in cpptraj-dev
    outfile->ProcessArgs("xstep " + integerToString(lagOffset_));
  }

  mprintf("    RMSAVGCORR: COORDS set [%s], mask [%s]", coords_->Legend().c_str(),
          mask_.MaskString());
  if (useMass_) mprintf(" (mass-weighted)");
  mprintf("\n");
  if (!refFilename.empty()) mprintf("\tReference '%s'\n", refFilename.c_str());
  if (maxwindow_!=-1) mprintf("\tMax window size %i\n",maxwindow_);
  if (lagOffset_ > 1) mprintf("\tWindow size offset %i\n", lagOffset_);
  if (outfile != 0) mprintf("\tOutput to %s\n",outfile->DataFilename().base());
  if (!separateName_.empty())
    mprintf("\tSeparate datafile will be written to %s\n",separateName_.c_str());
  return Analysis::OK;
}

/** Calculate the RMSD using running averages of coordinates in 
  * ReferenceCoords with different window sizes. The average RMSD for each 
  * window size will be the "correlation" value.
  */ 
Analysis::RetType Analysis_RmsAvgCorr::Analyze() {
  double avg, stdev, rmsd, d_Nwindow;
  int window, frame, WindowMax, widx, widx_end;
  CpptrajFile separateDatafile;
  int frameThreshold, subtractWindow;

  mprintf("    RMSAVGCORR:\n");
  // If 'output' specified open up separate datafile that will be written
  // to as correlation is calculated; useful for very long runs.
  if (!separateName_.empty()) {
    if (separateDatafile.OpenWrite(separateName_)) {
      mprinterr("Error: Could not set up separate data file %s\n",separateName_.c_str());
      return Analysis::ERR;
    }
  }
  // Set up mask
  if (coords_->Top().SetupIntegerMask( mask_ )) return Analysis::ERR;
  mask_.MaskInfo();
  if (mask_.None()) return Analysis::ERR;
  // Set up target frame for COORDS based on mask.
  Frame tgtFrame;
  tgtFrame.SetupFrameFromMask( mask_, coords_->Top().Atoms() );
  if (tgtFrame.Natom() != refFrame_.Natom()) {
    mprinterr("Error: Target mask %s (%i) does not correspond to reference mask (%i)\n",
              mask_.MaskString(), tgtFrame.Natom(), refFrame_.Natom());
    return Analysis::ERR;
  }
  // Pre-center reference
  refFrame_.CenterOnOrigin(useMass_);
  // Set up frame for holding sum of coordindates over window frames. 
  // No need for mass. 
  Frame sumFrame(mask_.Nselected());

  // Determine max window size to average over
  if (maxwindow_==-1)
    WindowMax = coords_->Size();
  else {
    WindowMax = maxwindow_+1;
    if (WindowMax > coords_->Size()) {
      WindowMax = coords_->Size();
      mprintf("Warning: RmsAvgCorr: stop (%i) > max # frames (%i), using max.\n",
              maxwindow_, WindowMax);
    }
  }

  // Print calc summary
  mprintf("\tPerforming RMSD calcs over running avg of coords with window\n"
          "\t  sizes ranging from 1 to %i, offset %i", WindowMax-1, lagOffset_);
  if (useMass_) mprintf(", mass-weighted");
  mprintf(".\n");

  // First value for Ct (window==1) is just the avg RMSD with no running 
  // averaging. Since all non-rms atoms have been stripped, all atoms in
  // ReferenceCoords will be used.
  // Pre-center reference
  // Calc initial RMSD
  avg = 0.0;
  stdev = 0.0;
  for (frame = 0; frame < coords_->Size(); frame++) {
    tgtFrame.SetFromCRD( (*coords_)[frame], 0, mask_);
    rmsd = tgtFrame.RMSD_CenteredRef(refFrame_, useMass_); 
    avg += rmsd;
    stdev += (rmsd * rmsd);
  }
  // DEBUG
/*
  sprintf(debugname,"dbgrmsd.dat");
  debug_data = new DataFile(debugname);
  debug_data->AddSet( rmsdaction.rmsd );
  debug_data->Write();
  delete debug_data;
*/
  d_Nwindow = 1.0 / (double)coords_->Size();
  avg *= d_Nwindow;
  stdev *= d_Nwindow;
  stdev -= (avg * avg);
  if (stdev > 0.0)
    stdev = sqrt( stdev );
  else
    stdev = 0.0;
  Ct_->Add(0, &avg);
  Csd_->Add(0, &stdev);
  if (!separateName_.empty())
    separateDatafile.Printf("%8i %f %f\n",1,avg,stdev);

  // Create an array with window sizes to be calcd.
  std::vector<int> w_sizes;
  int startWindow = 1 + lagOffset_; 
  int total_windows = (WindowMax - startWindow)  / lagOffset_;
  if ( (total_windows % lagOffset_) > 0 ) ++total_windows;
  w_sizes.reserve( total_windows );
  for (int ws = startWindow; ws < WindowMax; ws += lagOffset_)
    w_sizes.push_back( ws );
  // LOOP OVER DIFFERENT RUNNING AVG WINDOW SIZES
  widx_end = (int)w_sizes.size();
  ParallelProgress progress(widx_end);
# ifdef _OPENMP
  // Currently DataSet is not thread-safe. Use temp storage.
  double* Ct_openmp = new double[ widx_end ];
  double* Csd_openmp = new double[ widx_end ];
#pragma omp parallel private(widx,window,frame,avg,stdev,rmsd,frameThreshold,subtractWindow,d_Nwindow) firstprivate(tgtFrame,sumFrame,progress)
{
  progress.SetThread(omp_get_thread_num());
  if (omp_get_thread_num()==0)
    mprintf("\t\tParallelizing calculation with %i threads.\n",omp_get_num_threads());
#pragma omp for schedule(dynamic)
#endif
  for (widx = 0; widx < widx_end; widx++) {
    progress.Update(widx);
    // Initialize and set up running average for this window size
    window = w_sizes[widx];
    frameThreshold = window - 2;
    // TODO: Make subtractWindow a const iterator to CoordList
    subtractWindow = 0;
    d_Nwindow = (double) window;
    sumFrame.ZeroCoords();
    // LOOP OVER FRAMES
    avg = 0.0;
    stdev = 0.0;
    for (frame = 0; frame < coords_->Size(); frame++) {
      tgtFrame.SetFromCRD( (*coords_)[frame], 0, mask_);
      // Add current coordinates to sumFrame
      sumFrame += tgtFrame;
      // Do we have enough frames to start calculating a running avg?
      // If so, start calculating RMSDs
      if ( frame > frameThreshold ) {
        tgtFrame.Divide( sumFrame, d_Nwindow );
        rmsd = tgtFrame.RMSD_CenteredRef(refFrame_, useMass_);
        avg += rmsd;
        stdev += (rmsd * rmsd);
        // Subtract frame at subtractWindow from sumFrame 
        tgtFrame.SetFromCRD( (*coords_)[subtractWindow], 0, mask_);
        sumFrame -= tgtFrame;
        ++subtractWindow;
      }
    } // END LOOP OVER FRAMES
    // Take average rmsd for this window size. The number of frames for which
    // RMSD was calculated is (Total#frames) - (window size) + 1
    d_Nwindow = 1.0 / ((double)coords_->Size() - (double)window + 1.0);
    avg *= d_Nwindow;
    stdev *= d_Nwindow;
    stdev -= (avg * avg);
    if (stdev > 0.0)
      stdev = sqrt( stdev );
    else
      stdev = 0.0;
#   ifdef _OPENMP
    Ct_openmp[widx] = avg;
    Csd_openmp[widx] = stdev;
#   else 
    Ct_->Add(widx+1, &avg);
    Csd_->Add(widx+1, &stdev);
    if (!separateName_.empty())
      separateDatafile.Printf("%8i %f %f\n",window, avg, stdev);
#   endif
  } // END LOOP OVER WINDOWS
#ifdef _OPENMP
} // END pragma omp parallel
  // Put Ct_openmp into Ct dataset
  for (window = 0; window < widx_end; window++) {
    Ct_->Add(window+1, Ct_openmp+window);
    Csd_->Add(window+1, Csd_openmp+window);
  }
  delete[] Ct_openmp;
  delete[] Csd_openmp;
#endif
  progress.Finish();
  if (!separateName_.empty())
    separateDatafile.CloseFile();
  return Analysis::OK;
}
