#ifdef DATAFILE_TIME
#include <ctime>
#endif
#include "DataFile.h"
#include "CpptrajStdio.h"
// All DataIO classes go here
#include "DataIO_Std.h"
#include "DataIO_Grace.h"
#include "DataIO_Gnuplot.h"
#include "DataIO_Xplor.h"
#include "DataIO_OpenDx.h"

// TODO: Support these args:
//       - noemptyframes: Deprecate.
//       - xlabel, xmin, xstep, time (all dimensions).
// CONSTRUCTOR
DataFile::DataFile() :
  debug_(0),
  dimension_(-1),
  dataType_(DATAFILE),
  isInverted_(false),
  dataio_(0),
  Dim_(3) // default to X/Y/Z dims
{}

// DESTRUCTOR
DataFile::~DataFile() {
  if (dataio_ != 0) delete dataio_;
}

// ----- STATIC VARS / ROUTINES ------------------------------------------------
const DataFile::DataFileToken DataFile::DataFileArray[] = {
  { DATAFILE,     "dat",    "Standard Data File", ".dat",   DataIO_Std::Alloc     },
  { XMGRACE,      "grace",  "Grace File",         ".agr",   DataIO_Grace::Alloc   },
  { GNUPLOT,      "gnu",    "Gnuplot File",       ".gnu",   DataIO_Gnuplot::Alloc },
  { XPLOR,        "xplor",  "Xplor File",         ".xplor", DataIO_Xplor::Alloc   },
  { OPENDX,       "opendx", "OpenDx File",        ".dx",    DataIO_OpenDx::Alloc  },
  { UNKNOWN_DATA, 0,        "Unknown",            0,        0                     }
};

// DataFile::GetFormatFromArg()
/** Given an ArgList, search for one of the file format keywords. Default to
  * DATAFILE if no keywords present.
  */
DataFile::DataFormatType DataFile::GetFormatFromArg(ArgList& argIn) 
{
  for (TokenPtr token = DataFileArray; token->Type != UNKNOWN_DATA; ++token)
    if (argIn.hasKey( token->Key )) return token->Type;
  return DATAFILE;
}

// DataFile::GetFormatFromString()
DataFile::DataFormatType DataFile::GetFormatFromString(std::string const& fmt)
{
  for (TokenPtr token = DataFileArray; token->Type != UNKNOWN_DATA; ++token)
    if ( fmt.compare( token->Key )==0 ) return token->Type;
  return DATAFILE;
}

// DataFile::GetExtensionForType()
std::string DataFile::GetExtensionForType(DataFormatType typeIn) {
  for (TokenPtr token = DataFileArray; token->Type != UNKNOWN_DATA; ++token)
    if ( token->Type == typeIn )
      return std::string( token->Extension );
  return std::string();
}

// DataFile::GetTypeFromExtension()
DataFile::DataFormatType DataFile::GetTypeFromExtension( std::string const& extIn)
{
  for (TokenPtr token = DataFileArray; token->Type != UNKNOWN_DATA; ++token)
    if ( extIn.compare( token->Extension ) == 0 ) return token->Type;
  return UNKNOWN_DATA;
}

// DataFile::FormatString()
const char* DataFile::FormatString( DataFile::DataFormatType tIn ) {
  TokenPtr token;
  for (token = DataFileArray; token->Type != UNKNOWN_DATA; ++token)
    if ( token->Type == tIn ) return token->Description;
  return token->Description; // Should be at UNKNOWN
}
// -----------------------------------------------------------------------------

// DataFile::SetDebug()
void DataFile::SetDebug(int debugIn) {
  debug_ = debugIn;
  if (debug_ > 0) mprintf("\tDataFile debug level set to %i\n", debug_);
}

// ----- DATA FILE ALLOCATION / DETECTION ROUTINES -----------------------------
// DataFile::AllocDataIO()
DataIO* DataFile::AllocDataIO(DataFormatType tformat) {
  for (TokenPtr token = DataFileArray; token->Type != UNKNOWN_DATA; ++token) {
    if (token->Type == tformat) {
      if (token->Alloc == 0) {
        mprinterr("Error: CPPTRAJ was compiled without support for %s files.\n",
                  token->Description);
        return 0;
      } else
        return (DataIO*)token->Alloc();
    }
  }
  return 0;
}

// DataFile::DetectFormat()
DataIO* DataFile::DetectFormat(std::string const& fname) {
  CpptrajFile file;
  if (file.SetupRead(fname, 0)) return 0;
  for (TokenPtr token = DataFileArray; token->Type != UNKNOWN_DATA; ++token) {
    if (token->Alloc != 0) {
      DataIO* io = (DataIO*)token->Alloc();
      if ( io->ID_DataFormat( file ) )
        return io;
      delete io;
    }
  }
  return 0;
}

// DataFile::DataFormat()
DataFile::DataFormatType DataFile::DataFormat(std::string const& fname) {
  CpptrajFile file;
  if (file.SetupRead(fname, 0)) return UNKNOWN_DATA;
  for (TokenPtr token = DataFileArray; token->Type != UNKNOWN_DATA; ++token) {
    if (token->Alloc != 0) {
      DataIO* io = (DataIO*)token->Alloc();
      if ( io->ID_DataFormat( file ) ) {
        delete io;
        return token->Type;
      }
      delete io;
    }
  }
  return UNKNOWN_DATA;
}
// -----------------------------------------------------------------------------

// DataFile::ReadData()
int DataFile::ReadData(ArgList& argIn, DataSetList& datasetlist) {
  filename_.SetFileNameWithExpansion( argIn.GetStringNext() );
  dataio_ = DetectFormat( filename_.Full() );
  // Default to detection by extension.
  if (dataio_ == 0)
    dataio_ = AllocDataIO( GetTypeFromExtension(filename_.Ext()) );
  // Read data
  if ( dataio_->ReadData( filename_.Full(), datasetlist ) ) {
    mprinterr("Error reading datafile %s\n", filename_.Full().c_str());
    return 1;
  }

  return 0;
}

// DataFile::SetupDatafile()
int DataFile::SetupDatafile(std::string const& fnameIn, ArgList& argIn, int debugIn) {
  SetDebug( debugIn );
  if (fnameIn.empty()) return 1;
  filename_.SetFileName( fnameIn );
  // Set up DataIO based on format.
  // FIXME: Use DetectFormat()
  dataio_ = AllocDataIO( GetTypeFromExtension(filename_.Ext()) );
  if (dataio_ == 0) return 1;
  if (!argIn.empty())
    ProcessArgs( argIn );
  return 0;
}

// DataFile::AddSet()
int DataFile::AddSet(DataSet* dataIn) {
  if (dataIn == 0) return 1;
  if (SetList_.empty())
    dimension_ = dataIn->Ndim();
  else if ((int)dataIn->Ndim() != dimension_) {
    mprinterr("Error: DataSets in DataFile %s have dimension %i\n", 
              filename_.base(), dimension_);
    mprinterr("Error: Attempting to add set %s of dimension %u\n", 
              dataIn->Legend().c_str(), dataIn->Ndim());
    mprinterr("Error: Adding DataSets with different dimensions to same file\n");
    mprinterr("Error: is currently unsupported.\n");
    return 1;
  }
  SetList_.AddCopyOfSet( dataIn );
  return 0;
}

// DataFile::ProcessArgs()
int DataFile::ProcessArgs(ArgList &argIn) {
  if (dataio_==0) return 1;
  if (argIn.hasKey("invert")) {
    isInverted_ = true;
    // Currently GNUPLOT files cannot be inverted.
    if (dataType_ == GNUPLOT) {
      mprintf("Warning: (%s) Gnuplot files cannot be inverted.\n",filename_.base());
      isInverted_ = false;;
    }
  }
  // Axis args.
  if (argIn.Contains("xlabel"))
    Dim_[0].SetLabel( argIn.GetStringKey("xlabel") );
  if (argIn.Contains("ylabel"))
    Dim_[1].SetLabel( argIn.GetStringKey("ylabel") );
  // Axis min/step
  Dim_[0].SetMin( argIn.getKeyDouble("xmin", Dim_[0].Min()) );
  Dim_[1].SetMin( argIn.getKeyDouble("ymin", Dim_[1].Min()) );
  Dim_[0].SetStep( argIn.getKeyDouble("xstep", Dim_[0].Step()) );
  Dim_[1].SetStep( argIn.getKeyDouble("ystep", Dim_[1].Step()) );
  // ptraj 'time' keyword
  if (argIn.Contains("time")) {
    Dim_[0].SetStep( argIn.getKeyDouble("time", Dim_[0].Step()) );
    Dim_[0].SetMin( 0.0 );
    Dim_[0].SetOffset( 1 );
  }
  if (dataio_->processWriteArgs(argIn)==1) return 1;
  if (debug_ > 0) argIn.CheckForMoreArgs();
  return 0;
}

// DataFile::ProcessArgs()
int DataFile::ProcessArgs(std::string const& argsIn) {
  if (argsIn.empty()) return 1;
  ArgList args(argsIn);
  return ProcessArgs(args);
}

// DataFile::WriteData()
void DataFile::WriteData() {
  // Remove data sets that do not contain data.
  // All DataSets should have same dimension (enforced by AddSet()).
  DataSetList::const_iterator Dset = SetList_.end();
  while (Dset != SetList_.begin()) {
    --Dset;
    // Check if set has no data.
    if ( (*Dset)->Empty() ) {
      // If set has no data, remove it
      mprintf("Warning: Set %s contains no data. Skipping.\n",(*Dset)->Legend().c_str());
      SetList_.erase( Dset );
      Dset = SetList_.end();
    } else {
      // If set has data, set its format to right-aligned initially.
      if ( (*Dset)->SetDataSetFormat(false) ) {
        mprinterr("Error: could not set format string for set %s. Skipping.\n", 
                  (*Dset)->Legend().c_str());
        SetList_.erase( Dset );
        Dset = SetList_.end();
      } 
    }
  }
  // If all data sets are empty no need to write
  if (SetList_.empty()) {
    mprintf("Warning: file %s has no sets containing data.\n", filename_.base());
    return;
  }
  //mprintf("DEBUG:\tFile %s has %i sets, dimension=%i, maxFrames=%i\n", dataio_->FullFileStr(),
  //        SetList_.size(), dimenison_, maxFrames);
#ifdef DATAFILE_TIME
  clock_t t0 = clock();
#endif
  if ( dimension_ == 1 ) {
    // Set min if not already set. 
    if (Dim_[0].Min() == 0 && Dim_[0].Max() == 0) 
      Dim_[0].SetMin( 1.0 );
    // Set step if not already set.
    if (Dim_[0].Step() < 0)
      Dim_[0].SetStep( 1.0 );
    // Set label if not already set.
    if (Dim_[0].Label().empty())
      Dim_[0].SetLabel("Frame");
    mprintf("%s: Writing 1D data.\n",filename_.base());
    if (!isInverted_)
      dataio_->WriteData(filename_.Full(), SetList_, Dim_);
    else
      dataio_->WriteDataInverted(filename_.Full(), SetList_, Dim_);
  } else if ( dimension_ == 2) {
    mprintf("%s: Writing 2D data.\n",filename_.base());
    int err = 0;
    for ( DataSetList::const_iterator set = SetList_.begin();
                                      set != SetList_.end(); ++set)
      err += dataio_->WriteData2D(filename_.Full(), *(*set), Dim_ );
    if (err > 0) 
      mprinterr("Error writing 2D DataSets to %s\n", filename_.base());
  }
#ifdef DATAFILE_TIME
  clock_t tf = clock();
  mprinterr("DataFile %s Write took %f seconds.\n", filename_.base(),
            ((float)(tf - t0)) / CLOCKS_PER_SEC);
#endif
}

// DataFile::SetPrecision()
/** Set precision for all DataSets in file to width.precision. */
void DataFile::SetPrecision(int widthIn, int precisionIn) {
  for (DataSetList::const_iterator set = SetList_.begin(); set != SetList_.end(); ++set)
    (*set)->SetPrecision(widthIn, precisionIn);
}

// DataFile::DataSetNames()
/** Print Dataset names to one line. If the number of datasets is greater 
  * than 10 just print the first and last 4 data sets.
  */
void DataFile::DataSetNames() {
  DataSetList::const_iterator set = SetList_.begin();
  if (SetList_.size() > 10) {
    int setnum = 0;
    while (setnum < 4) {
      mprintf(" %s",(*set)->Legend().c_str());
      ++setnum;
      ++set;
    }
    mprintf(" ...");
    set = SetList_.end() - 4;
    setnum = 0;
    while (setnum < 4) {
      mprintf(" %s",(*set)->Legend().c_str());
      ++setnum;
      ++set;
    }
  } else {
    for (; set != SetList_.end(); set++)
      mprintf(" %s",(*set)->Legend().c_str());
  }
}
