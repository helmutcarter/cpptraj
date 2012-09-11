#include <cmath> // floor
#include "ImageRoutines.h"
#include "DistRoutines.h"

// SetupImageTruncoct()
/** Set up centering if putting nonortho cell into familiar trunc. oct. shape.
  * \param frameIn Frame to set up for.
  * \param ComMask If not NULL center is calcd w.r.t. center of atoms in mask.
  * \param useMass If true calculate COM, otherwise calc geometric center.
  * \param origin If true and ComMask is NULL use origin, otherwise use box center.
  * \return Coordinates of center.
  */
Vec3 SetupImageTruncoct( Frame& frameIn, AtomMask* ComMask, bool useMass, bool origin)
{
  if (ComMask!=NULL) {
    // Use center of atoms in mask
    if (useMass)
      return frameIn.VCenterOfMass( *ComMask );
    else
      return frameIn.VGeometricCenter( *ComMask );
  } else if (!origin) {
    // Use box center
    return Vec3( frameIn.BoxX() / 2, frameIn.BoxY() / 2, frameIn.BoxZ() / 2 );
  }
  //fprintf(stdout,"DEBUG: fcom = %lf %lf %lf\n",fcom[0],fcom[1],fcom[2]);
  return Vec3(); // Default is origin {0,0,0}
}

// ImageNonortho()
/** \param frameIn Frame to image.
  * \param origin If true image w.r.t. coordinate origin.
  * \param fcom If truncoct is true, calc distance w.r.t. this coordinate.
  * \param ucell Unit cell matrix.
  * \param recip Reciprocal coordinates matrix.
  * \param truncoct If true imaging will occur using truncated octahedron shape.
  * \param center If true image w.r.t. center coords, otherwise use first atom coords.
  * \param useMass If true use COM, otherwise geometric center.
  * \param AtomPairs Atom pairs to image.
  */
void ImageNonortho(Frame& frameIn, bool origin, Vec3 const& fcom, 
                   Matrix_3x3 ucell, Matrix_3x3 recip, // TODO: Make const &
                   bool truncoct, bool center,
                   bool useMass, std::vector<int> const& AtomPairs)
{
  Vec3 Coord;
  double min = -1.0;

  if (truncoct)
    min = 100.0 * (frameIn.BoxX()*frameIn.BoxX()+
                   frameIn.BoxY()*frameIn.BoxY()+
                   frameIn.BoxZ()*frameIn.BoxZ());

  // Loop over atom pairs
  for (std::vector<int>::const_iterator atom = AtomPairs.begin();
                                        atom != AtomPairs.end(); ++atom)
  {
    int firstAtom = *atom;
    ++atom;
    int lastAtom = *atom;
    //if (debug>2)
    //  mprintf( "  IMAGE processing atoms %i to %i\n", firstAtom+1, lastAtom);
    // Set up Coord with position to check for imaging based on first atom or 
    // center of mass of atoms first to last.
    if (center) {
      if (useMass)
        Coord = frameIn.VCenterOfMass(firstAtom,lastAtom);
      else
        Coord = frameIn.VGeometricCenter(firstAtom,lastAtom);
    } else 
      Coord = frameIn.XYZ( firstAtom );

    // boxTrans will hold calculated translation needed to move atoms back into box
    Vec3 boxTrans = ImageNonortho(Coord, truncoct, origin, ucell, recip, fcom, min);

    frameIn.Translate(boxTrans, firstAtom, lastAtom);

  } // END loop over atom pairs
}

// ImageNonortho()
/** \param Coord Coordinate to image.
  * \param truncoct If true, image in truncated octahedral shape.
  * \param origin If true, image w.r.t. coordinate origin.
  * \param ucell Unit cell matrix.
  * \param recip Reciprocal coordinates matrix.
  * \param fcom If truncoct, image translated coordinate w.r.t. this coord.
  * \return Vector containing image translation.
  */
Vec3 ImageNonortho(Vec3 const& Coord, bool truncoct, 
                   bool origin, const Matrix_3x3& ucell, const Matrix_3x3& recip, 
                   Vec3 const& fcom, double min)
{
  int ixyz[3];

  Vec3 fc = Coord;
  recip.MultVec( fc );

  if ( origin )
    fc += 0.5; 

  Vec3 boxTransOut(floor(fc[0]), floor(fc[1]), floor(fc[2]));
  ucell.TransposeMultVec( boxTransOut );
  boxTransOut.Neg();

  // Put into familiar trunc. oct. shape
  if (truncoct) {
    Vec3 TransCoord = Coord;
    TransCoord += boxTransOut;
    recip.MultVec( TransCoord ); 
    Vec3 f2 = fcom;
    recip.MultVec( f2 );

    if (origin) {
      TransCoord += 0.5;
      f2 += 0.5;
    }

    DIST2_ImageNonOrthoRecip(TransCoord.Dptr(), f2.Dptr(), min, ixyz, ucell.Dptr());
    if (ixyz[0] != 0 || ixyz[1] != 0 || ixyz[2] != 0) {
      Vec3 vixyz( ixyz );
      ucell.TransposeMultVec( vixyz );
      boxTransOut += vixyz;
      //if (debug > 2)
      //  mprintf( "  IMAGING, FAMILIAR OFFSETS ARE %i %i %i\n", 
      //          ixyz[0], ixyz[1], ixyz[2]);
    }
  }
  return boxTransOut;
}

// SetupImageOrtho()
/** \param frameIn Frame to image.
  * \param bp Output: Box + boundary.
  * \param bm Output: Box - boundary.
  * \param origin If true, image w.r.t. coordinate origin, otherwise box center.
  */
void SetupImageOrtho(Frame& frameIn, Vec3& bp, Vec3& bm, bool origin) {
  // Set up boundary information for orthorhombic cell
  if (origin) {
    bp.SetVec( frameIn.BoxX() / 2,
               frameIn.BoxY() / 2,
               frameIn.BoxZ() / 2 );
    bm.SetVec( -bp[0], -bp[1], -bp[2] );
  } else {
    bp.SetVec( frameIn.BoxX(),
               frameIn.BoxY(),
               frameIn.BoxZ()  );
    bm.Zero();
  }
}

// ImageOrtho()
/** \param frameIn Frame to image.
  * \param bp Box + boundary.
  * \param bm Box - boundary.
  * \param center If true image w.r.t. center of atoms, otherwise first atom.
  * \param useMass If true calc center of mass, otherwise geometric center.
  */
void ImageOrtho(Frame& frameIn, Vec3 const& bp, Vec3 const& bm, bool center, bool useMass,
                std::vector<int> const& AtomPairs)
{
  Vec3 Coord;
  Vec3 BoxVec( frameIn.BoxX(), frameIn.BoxY(), frameIn.BoxZ() );

  // Loop over atom pairs
  for (std::vector<int>::const_iterator atom = AtomPairs.begin();
                                        atom != AtomPairs.end(); atom++)
  {
    int firstAtom = *atom;
    ++atom;
    int lastAtom = *atom;
    //if (debug>2)
    //  mprintf( "  IMAGE processing atoms %i to %i\n", firstAtom+1, lastAtom);
    // Set up Coord with position to check for imaging based on first atom or 
    // center of mass of atoms first to last.
    if (center) {
      if (useMass)
        Coord = frameIn.VCenterOfMass(firstAtom,lastAtom);
      else
        Coord = frameIn.VGeometricCenter(firstAtom,lastAtom);
    } else 
      Coord = frameIn.XYZ( firstAtom );

    // boxTrans will hold calculated translation needed to move atoms back into box
    Vec3 boxTrans = ImageOrtho(Coord, bp, bm, BoxVec);

    // Translate atoms according to Coord
    frameIn.Translate(boxTrans, firstAtom, lastAtom);
  } // END loop over atom pairs
}

// ImageOrtho()
/** \param Coord Coordinate to image
  * \param bp Box + boundary
  * \param bm Box - boundary
  * \param BoxVec box lengths.
  * \return Vector containing image translation
  */
Vec3 ImageOrtho(Vec3 const& Coord, Vec3 const& bp, Vec3 const& bm, Vec3 const& BoxVec)
{
  double trans[3];
  // Determine how far Coord is out of box
  for (int i = 0; i < 3; ++i) {
    trans[i] = 0.0;
    double crd = Coord[i];
    while (crd < bm[i]) {
      crd += BoxVec[i];
      trans[i] += BoxVec[i];
    }
    while (crd > bp[i]) {
      crd -= BoxVec[i];
      trans[i] -= BoxVec[i];
    }
  }
  return Vec3( trans );
}

// UnwrapNonortho()
void UnwrapNonortho( Frame& frameIn, Frame& ref, AtomMask const& mask,
                     Matrix_3x3 const& ucell, Matrix_3x3 const& recip ) 
{
  for (AtomMask::const_iterator atom = mask.begin();
                                atom != mask.end(); ++atom)
  {
    int i3 = *atom * 3;
    Vec3 vtgt = frameIn.CRD( i3 );
    double minX = vtgt[0];
    double minY = vtgt[1];
    double minZ = vtgt[2];
    Vec3 vref = ref.CRD( i3 );

    Vec3 vd = vtgt - vref; // dx dy dz
    double minDistanceSquare = vd.Magnitude2();

    recip.MultVec( vd ); // recip * dxyz

    double cx = floor(vd[0]);
    double cy = floor(vd[1]);
    double cz = floor(vd[2]);

    for (int ix = -1; ix < 2; ++ix) {
      for (int iy = -1; iy < 2; ++iy) {
        for (int iz = -1; iz < 2; ++iz) {
          Vec3 vcc( cx + (double)ix, cy + (double)iy, cz + (double) iz ); // ccx ccy ccz

          ucell.TransposeMultVec( vcc ); // ucell^T * ccxyz

          Vec3 vnew = vtgt - vcc; 
 
          Vec3 vr = vref - vnew; 
  
          double distanceSquare = vr.Magnitude2();

          if ( minDistanceSquare > distanceSquare ) {
              minDistanceSquare = distanceSquare;
              minX = vnew[0];
              minY = vnew[1];
              minZ = vnew[2];
          }
        }
      }
    }
    ref[i3  ] = frameIn[i3  ] = minX; 
    ref[i3+1] = frameIn[i3+1] = minY;
    ref[i3+2] = frameIn[i3+2] = minZ;

  } // END loop over selected atoms
}

// UnwrapOrtho()
void UnwrapOrtho( Frame& frameIn, Frame& ref, AtomMask const& mask ) {
  double boxX = frameIn.BoxX();
  double boxY = frameIn.BoxY();
  double boxZ = frameIn.BoxZ();
  for (AtomMask::const_iterator atom = mask.begin();
                                atom != mask.end(); ++atom)
  {
    int i3 = *atom * 3;
    double dx = frameIn[i3  ] - ref[i3  ];
    double dy = frameIn[i3+1] - ref[i3+1];
    double dz = frameIn[i3+2] - ref[i3+2];

    ref[i3  ] = frameIn[i3  ] = frameIn[i3  ] - floor( dx / boxX + 0.5 ) * boxX;
    ref[i3+1] = frameIn[i3+1] = frameIn[i3+1] - floor( dy / boxY + 0.5 ) * boxY;
    ref[i3+2] = frameIn[i3+2] = frameIn[i3+2] - floor( dz / boxZ + 0.5 ) * boxZ;
  }
}
