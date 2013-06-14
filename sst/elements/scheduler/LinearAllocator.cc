// Copyright 2009-2013 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2013, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

/**
 * Abstract class to act as superclass for allocators based on curves->
 *
 * Format for file giving curve:
 * list of pairs of numbers (separated by whitespace)
 *   the first member of each pair is the processor 0-indexed
 *     rank if the coordinates are treated as a 3-digit number
 *     (z coord most significant, x coord least significant)
 *     these values should appear in order
 *   the second member of each pair gives its rank in the desired order
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sstream>
#include <time.h>
#include <math.h>

#include "sst/core/serialization/element.h"

#include "LinearAllocator.h"
#include "Machine.h"
#include "MachineMesh.h"
#include "AllocInfo.h"
#include "Job.h"
#include "misc.h"

#define DEBUG false
#define XROTS(point1) point1
#define YROTS(point1) 8 + point1
#define ZROTS(point1) 16 + point1
#define MIRROR(point1) 24 + point1
#define INVERSE(point1) 32 + point1

using namespace SST::Scheduler;


//takes coordinate (x,y), offsets it by (xoff,yoff) where the offset is rotated
//by rotate*90 degrees, and the offset may or may not be mirrored.  Used for
//2D Hilbert curve generation
int LinearAllocator::MeshLocationOrdering::valtox(int x, int xoff, int y, int yoff, int rotate, int mirror)
{
    switch (rotate) {
    case 0: return x + mirror * xoff;
    case 1: return x - mirror * yoff;
    case 2: return x - mirror * xoff;
    case 3: return x + mirror * yoff;
    default: error("rotate value too large in LinearAllocator.cc:" + rotate);
             return 0;
    } 

}
int LinearAllocator::MeshLocationOrdering::valtoy(int x, int xoff, int y, int yoff, int rotate, int mirror)
{
    switch (rotate) {
    case 0: return y + mirror * yoff;
    case 1: return y - mirror * xoff;
    case 2: return y - mirror * yoff;
    case 3: return y + mirror * xoff;
    default: error("rotate value too large in LinearAllocator.cc:" + rotate);
             return 0;
    } 
}

//helper function for 3D Hilbert curve
//takes in a 3D point and offsets it by offset along the axis given by coordinate
//where this is all rotated by rot.  The answer is stored in destination.
LinearAllocator::MeshLocationOrdering::triple LinearAllocator::MeshLocationOrdering::addwithrotate(triple origin, int coordinate, int offset, int mirror, rotation rot)
{
    triple dest;
    for (int x = 0; x < 3; x++) {
        if (((mirror & (1 << x)) != 0)) {
            dest.d[x] = (rot.r[3 * x + coordinate] * offset * -1 + origin.d[x]);
        } else {
            dest.d[x] = (rot.r[3 * x + coordinate] * offset + origin.d[x]);
        }
    }
    return dest;
}

//helper function for 3D Hilbert curve
//marks the 8 points with the correct rank, and updates the inverse rotation and next matrices accordingly
void LinearAllocator::MeshLocationOrdering::threedmark(triple curpoint, rotation* rot, int* rank, int curdim, triple* next, int* mirrors, int* inverse, int* rules)
{

    int curindex = tripletoint(curpoint);

    //set up constants for x, y, and z rotation
    //these rotate clockwise assuming a right-handed coordinate system
    int xar[9] = {1, 0, 0,
        0, 0, 1,
        0, -1, 0};

    int yar[9] = {0, 0, -1,
        0, 1, 0,
        1, 0, 0};

    int zar[9] = {0, 1, 0,
        -1, 0, 0,
        0, 0, 1};

    rotation XROT;
    rotation YROT;
    rotation ZROT;
    rotation NOROT;
    for (int x = 0; x < 9; x++) {
        XROT.r[x] = xar[x];
        YROT.r[x] = yar[x];
        ZROT.r[x] = zar[x];
        NOROT.r[x] = 0;
    }
    NOROT.r[0] = 1;
    NOROT.r[4] = 1;
    NOROT.r[8] = 1;

    rotation bigrotate = rot[curindex];
    int bigmirror = mirrors[curindex];
    int biginverse = inverse[curindex];
    int lastrank = 0;
    triple curpoint2 = curpoint;
    triple originalnext = next[curindex];

    if (1 == biginverse) {
        for (int point = 0; point < 8; point++) {
            curpoint = curpoint2;

            //set rotation
            rotation rotate = NOROT;
            for (int rots = 0; rots < rules[XROTS(point)]; rots++) {
                rotate = XROT * rotate;
            }
            for (int rots = 0; rots < rules[YROTS(point)]; rots++) {
                rotate = YROT * rotate;
            }
            for (int rots = 0; rots < rules[ZROTS(point)]; rots++) {
                rotate = ZROT * rotate;
            }
            rot[tripletoint(curpoint)] = bigrotate * rotate;

            //set mirror
            //our new mirror is rotated by bigrotate
            triple mirror;
            mirror.d[0] = 0;
            mirror.d[1] = 0;
            mirror.d[2] = 0;
            int localmirror = rules[MIRROR(point)];
            mirror = addwithrotate(mirror, 0, localmirror & 1, 0, bigrotate);
            mirror = addwithrotate(mirror, 1, localmirror & 2, 0, bigrotate);
            mirror = addwithrotate(mirror, 2, localmirror & 4, 0, bigrotate);
            localmirror = (int)(mirror.d[0] != 0) + 2 * (int)(mirror.d[1] != 0) + 4 * (int)(mirror.d[2] != 0); //don't want negative values 
            mirrors[tripletoint(curpoint)] = bigmirror ^ localmirror;

            //set inverse
            inverse[tripletoint(curpoint)] = biginverse * rules[INVERSE(point)];

            //set rank
            if (point > 0) {
                rank[tripletoint(curpoint)] = lastrank + curdim * curdim * curdim / 8;
            } 
            lastrank = rank[tripletoint(curpoint)];

            //find next point
            curpoint2 = addwithrotate(curpoint, 0, 2 * curdim / 6 * inverse[tripletoint(curpoint)],  mirrors[tripletoint(curpoint)], rot[tripletoint(curpoint)]);
            curpoint2 = addwithrotate(curpoint2, 2, -1 * curdim / 6 * inverse[tripletoint(curpoint)], mirrors[tripletoint(curpoint)], rot[tripletoint(curpoint)]);

            //we either step in the negative Z or negative X direction if we're not inverted or inverted, respectively
            if (inverse[tripletoint(curpoint)] == 1) {
                curpoint2 = addwithrotate(curpoint2, 2, -1, mirrors[tripletoint(curpoint)], rot[tripletoint(curpoint)]);
            } else {
                curpoint2 = addwithrotate(curpoint2, 0, -1, mirrors[tripletoint(curpoint)], rot[tripletoint(curpoint)]);
            }
            next[tripletoint(curpoint)] = curpoint2;
        }
        //the last point has a different next
        next[tripletoint(curpoint)] = originalnext;
    } else {
        for (int point = 7; point >= 0; point--) {
            curpoint = curpoint2;

            //set rotation
            rotation rotate = NOROT;
            for (int rots = 0; rots < rules[XROTS(point)]; rots++) {
                rotate = XROT * rotate;
            }
            for (int rots = 0; rots < rules[YROTS(point)]; rots++) {
                rotate = YROT * rotate;
            }
            for (int rots = 0; rots < rules[ZROTS(point)]; rots++) {
                rotate = ZROT * rotate;
            }
            rot[tripletoint(curpoint)] = bigrotate * rotate;

            //set mirror
            //our new mirror is rotated by bigrotate
            triple mirror;
            mirror.d[0] = 0;
            mirror.d[1] = 0;
            mirror.d[2] = 0;
            int localmirror = rules[MIRROR(point)];
            mirror = addwithrotate(mirror, 0, localmirror & 1, 0, bigrotate);
            mirror = addwithrotate(mirror, 1, localmirror & 2, 0, bigrotate);
            mirror = addwithrotate(mirror, 2, localmirror & 4, 0, bigrotate);
            localmirror = (int)(mirror.d[0] != 0) + 2 * (int)(mirror.d[1] != 0) + 4 * (int)(mirror.d[2] != 0); //don't want negative values 
            mirrors[tripletoint(curpoint)] = bigmirror ^ localmirror;

            //set inverse
            inverse[tripletoint(curpoint)] = biginverse * rules[INVERSE(point)];

            //set rank
            if (point < 7) {
                rank[tripletoint(curpoint)] = lastrank + curdim * curdim * curdim / 8;
            } 
            lastrank = rank[tripletoint(curpoint)];

            //find next point
            curpoint2 = addwithrotate(curpoint, 0, 2 * curdim / 6 * inverse[tripletoint(curpoint)],  mirrors[tripletoint(curpoint)], rot[tripletoint(curpoint)]);
            curpoint2 = addwithrotate(curpoint2, 2, -1 * curdim / 6 * inverse[tripletoint(curpoint)], mirrors[tripletoint(curpoint)], rot[tripletoint(curpoint)]);

            //we either step in the negative Z or negative X direction if we're not inverted or inverted, respectively
            if (inverse[tripletoint(curpoint)] == 1) {
                curpoint2 = addwithrotate(curpoint2, 2, -1, mirrors[tripletoint(curpoint)], rot[tripletoint(curpoint)]);
            } else {
                curpoint2 = addwithrotate(curpoint2, 0, -1, mirrors[tripletoint(curpoint)], rot[tripletoint(curpoint)]);
            }
            next[tripletoint(curpoint)] = curpoint2;
        }
        next[tripletoint(curpoint)] = originalnext;
    }
}

LinearAllocator::MeshLocationOrdering::MeshLocationOrdering(Machine* mach, bool SORT = false, bool hilbert = true) 
{
    MachineMesh* m = dynamic_cast<MachineMesh*>(mach);
    if (NULL == m) error("Linear Allocators Require Mesh Machine");

    if (SORT) {
        set<int> dimordering;
        set<int>::iterator xit = dimordering.insert(m -> getXDim()).first;
        set<int>::iterator yit = dimordering.insert(m -> getYDim()).first;
        set<int>::iterator zit = dimordering.insert(m -> getZDim()).first;
        set<int>::iterator it = dimordering.begin();
        xpos = distance(dimordering.begin(), xit);
        ypos = distance(dimordering.begin(), yit);
        zpos = distance(dimordering.begin(), zit);
        xdim = *it++;
        ydim = *it++;
        zdim = *it;
    } else {
        xpos = 0;
        ypos = 1;
        zpos = 2;
        xdim = m -> getXDim();
        ydim = m -> getYDim();
        zdim = m -> getZDim();
    }

    if (hilbert) {
        //Hilbert curve
        if (1 == zdim) {
            //Seperate case for the 2D grid as the 3D version does not
            //necessarily produce the 2D analog when limited to a 2D grid
            int xdimh, ydimh;
            if (xdim > ydim) {
                xdimh = xdim;
            } else {
                xdimh = ydim;
            }

            //if not power of 2, round up
            int xdimhtemp = xdimh;
            xdimh = 1;
            while (xdimh < xdimhtemp) {
                xdimh*=2;
            }
            ydimh = xdimh;

            rank = new int[xdim * ydim * zdim];

            //rankh is assumed to be 2^n by 2^n.  The actual rank array will be calculated
            //based on rankh.
            int * rankh = new int[xdimh * ydimh * zdim];

            //these three are useful in recursively calculating the curve
            int* rotate = new int[xdimh * ydimh * zdim];
            int* mirror = new int[xdimh * ydimh * zdim];
            int* next = new int[xdimh * ydimh * zdim];

            for (int x = 0; x < xdimh * ydimh * zdim; x++) {
                rankh[x] = 0;
                next[x] = 0;
            }

            rankh[0] = 0;
            rotate[0] = 0;
            mirror[0] = 1;
            next[0] = 0;

            int curx = 0;
            int cury = 0;
            int curxd = xdimh;
            int curyd = ydimh;

            while(curxd > 1 && curyd > 1) {
                if ((curxd % 2 != 0 && 1 != curxd) || (curyd % 2 != 0 && 1 != curyd))
                    error("Hilbert Curve requires dimensions to be powers of two currently");

                //mark the four points appropriately

                int size = curxd * curyd / 4;

                //point 1 already has a rankh filled in
                int point1 = curx + cury * xdimh;

                //point 2
                int point2 = valtox(curx, 0, cury, curyd / 2, rotate[point1], mirror[point1]) + 
                    xdimh * valtoy(curx, 0, cury, curyd / 2, rotate[point1], mirror[point1]); 

                rankh[point2] = rankh[point1] + 1 * size;
                rotate[point2] = rotate[point1];
                mirror[point2] = mirror[point1];

                //point 3
                int point3 = valtox(curx, curxd / 2, cury, curyd / 2, rotate[point1], mirror[point1]) + 
                    xdimh * valtoy(curx, curxd / 2, cury, curyd / 2, rotate[point1], mirror[point1]); 

                rankh[point3] = rankh[point1] + 2 * size;
                rotate[point3] = rotate[point1];
                mirror[point3] = mirror[point1];

                //point 4
                int point4 = valtox(curx, curxd - 1, cury, curyd / 2 - 1, rotate[point1], mirror[point1]) + xdimh * valtoy(curx, curxd - 1, cury, curyd / 2 - 1, rotate[point1], mirror[point1]);

                rankh[point4] = rankh[point1] + 3 * size;
                rotate[point4] = (rotate[point1] + 3 * mirror[point1]) % 4;
                if (rotate[point4] < 0) rotate[point4] *= -1;
                mirror[point4] = -1 * mirror[point1];

                //without modifications, if our grid is not rectangular we can wind
                //up with very discontinous sections.  for example, the n x n/2
                //grid has a jump of size n between consecutive points (fairly
                //unacceptable).  As a result, when we would cut a square in half
                //horizontally, we rotate the bottom-two subsquares up so that the
                //entrance and exits to the top squares are touching.  Then we flip
                //them vertically, so the entrance and exit to the larger square
                //remains the same.  This will mess up continuity for points 2 and
                //3 recursively but we won't use them anyway there can still be
                //some discontinuities, all along the right border, but none of the
                //jumps appear to be larger than 2 
                if (0 == rotate[point1] % 2) {
                    if (valtoy(curx, curxd / 2, cury, curyd / 2, rotate[point1], mirror[point1]) == ydim) {
                        rankh[point4] = 0; //undo our old point 4
                        point4 = valtox(curx, curxd/2, cury, 0, rotate[point1], mirror[point1]) + 
                            xdimh * valtoy(curx, curxd/2, cury, 0, rotate[point1], mirror[point1]); 
                        rankh[point4] = rankh[point1] + 3 * size;
                        rotate[point4] = rotate[point1];
                        mirror[point4] = mirror[point1];
                        //point 1 is not rotated or mirrored
                    } else {
                        //without rectangle shenanigans, point1 is rotated and
                        //mirrored at the next level of detail
                        rotate[point1] += 1 * mirror[point1];
                        rotate[point1] %= 4;
                        if (rotate[point1] < 0) rotate[point1] *= -1;
                        mirror[point1] *= -1;
                    }
                } else {
                    if (valtox(curx, curxd / 2, cury, curyd / 2,rotate[point1], mirror[point1]) == xdim) {
                        rankh[point4] = 0; //undo our old point 4
                        point4 = valtox(curx, curxd / 2, cury, 0, rotate[point1], mirror[point1]) + 
                            xdimh * valtoy(curx, curxd / 2, cury, 0, rotate[point1], mirror[point1]); 
                        rankh[point4] = rankh[point1] + 3 * size;
                        rotate[point4] = rotate[point1];
                        mirror[point4] = mirror[point1];
                        //point 1 is not rotated or mirrored

                    } else {
                        //without rectangle shenanigans, point1 is rotated and
                        //mirrored at the next level of detail
                        rotate[point1] += 1 * mirror[point1];
                        rotate[point1] %= 4;
                        if (rotate[point1] < 0) rotate[point1] *= -1;
                        mirror[point1] *= -1;
                    }
                }


                //set next points for when we recurse
                int nextpoint = next[point1];  //don't overwrite where we're going next
                next[point1] = point2;
                next[point2] = point3;
                next[point3] = point4;
                next[point4] = nextpoint;

                //go to the next point
                curx = nextpoint % xdimh;
                cury = nextpoint / xdimh;

                //printf("1: %d|%d|%d 2: %d|%d|%d 3: %d|%d|%d 4: %d|%d|%d\n", point1,rotate[point1],mirror[point1],point2,rotate[point2],mirror[point2],point3,rotate[point3],mirror[point3],point4,rotate[point4],mirror[point4]);

                if (nextpoint == 0) {
                    curxd /= 2;
                    curyd /= 2;

                    if (DEBUG) {
                        //print the current rankhings
                        printf("\n");
                        for(int y = ydimh - 1; y >= 0; y--) {
                            for(int x = 0; x < xdimh; x++) {
                                printf("%3d ", rankh[x + y * xdimh]);
                                if((x+1) % curxd == 0)
                                    printf("|");
                            }
                            printf("\n");
                            if(y % curyd == 0) {
                                for(int z = 0; z < 4 * xdimh + xdimh / curxd; z++)
                                    printf("-");
                                printf("\n");
                            }
                        }
                    }
                }
            }
            delete [] rotate;
            delete [] mirror;
            delete [] next;

            //our rankh array is full
            //we copy the values over to the rank array, then
            //compress them so they are consecutive
            for (int x = 0; x < xdim; x++) {
                for (int y = 0; y < ydim; y++) {
                    rank[x + y * xdim] = rankh[x + y * xdimh];
                }
            }
            int* exists = new int[xdimh * ydimh];
            for (int x = 0; x < xdimh * ydimh; x++) {
                exists[x] = 0;
            }
            for (int x = 0; x < xdim * ydim; x++) {
                exists[rank[x]] = 1;
            }
            for (int x = 1; x < xdimh * ydimh; x++) {
                exists[x] = exists[x-1] + exists[x];
            }
            for (int x = 0; x < xdim * ydim; x++) {
                rank[x] = exists[rank[x]]-1;
            }

            delete [] exists;
            delete [] rankh;

            if (DEBUG) {
                //print rank
                printf("\n");
                for(int y = ydim - 1; y >= 0; y--) {
                    for(int x = 0; x < xdim; x++) {
                        printf("%3d ", rank[x + y * xdim]);
                    }
                    printf("\n");
                }
            }
            exit(0);
        } else { 

            //a 3D Hilbert curve. There are many ways to extend a Hilbert curve
            //to three dimensions.  This one is from "An inventory of
            //three-dimensional Hilbert space-filling curves" by Herman
            //Haverkort.  It is called the F curve and has among the best
            //locality measures.  It is also one of the most strongly bending
            //curves.  However, it is not vertex-gated (does not start and end
            //at verices of the cube like the 2D version), is somewhat
            //difficult due to its mirrors and rotations, and does not give a
            //Hilbert curve when limited to a 2D mesh.

            printf("making 3D Hilbert curve\n");

            rank = new int[xdim * ydim * zdim];

            int realxdim = xdim;
            int realydim = ydim;
            int realzdim = zdim;
            xdim = 1;
            while (xdim < realxdim || xdim < realydim || xdim < realzdim) {
                xdim *= 2;
            }
            ydim = xdim;
            zdim = xdim;

            int * rankh = new int[xdim * ydim * zdim];
            rotation* rot= new rotation[xdim * ydim * zdim];
            triple* next = new triple[xdim * ydim * zdim];
            int* mirrors = new int[xdim * ydim * zdim];
            int* inverse = new int[xdim * ydim * zdim];

            int curdim = xdim;
            triple curpoint; 
            curpoint.d[0] = 0;
            curpoint.d[1] = curdim / 3;
            curpoint.d[2] = curdim / 3;
            next[tripletoint(curpoint)] = curpoint;
            rankh[tripletoint(curpoint)] = 0;
            inverse[tripletoint(curpoint)] = 1;
            for (int x = 0; x < 9; x++) {
                rot[tripletoint(curpoint)].r[x] = 0;
            }
            rot[tripletoint(curpoint)].r[0] = 1;
            rot[tripletoint(curpoint)].r[4] = 1;
            rot[tripletoint(curpoint)].r[8] = 1;
            mirrors[tripletoint(curpoint)] = 0;

            //the rules for the F curve.
            //for each of the eight recursive subcalls it says how it's rotated
            //and mirrored, and whether or not it's inverted.  For example,
            //point 2 is rotated twice clockwise around the X axis, once around
            //the Z axis, is mirrored along the Y axis and is not inverted.
            //Currently these cannot be changed (as the starting points are
            //hard-coded); if any of these numbers changes the algorithm will
            //break completely.  With different starting points there are other
            //possible rulesets that would work; however, most arbitrary
            //rulesets will not work
            int rules[40] = {1, 2, 0, 3, 3, 0, 2, 0, //X rotations
                0, 0, 0, 0, 0, 0, 0, 3, //Y rotations
                2, 1, 3, 1, 3, 1, 3, 1, //Z rotations
                1, 2, 0, 0, 2, 2, 0, 4, //mirrors
                1, 1,-1, 1,-1, 1,-1,-1}; //inverses

            while (curdim > 1) {
                //mark the eight points accordingly
                triple nextpoint = next[tripletoint(curpoint)]; //don't overwrite 
                threedmark(curpoint, rot, rankh, curdim, next, mirrors, inverse, &rules[0]);

                //go to the next point
                curpoint = nextpoint;

                //once we're back to the original point, go down a dimension
                if (0 == curpoint.d[0] && ydim / 3 == curpoint.d[1] && zdim / 3 == curpoint.d[2])
                    curdim /= 2;
            }

            //now squish the points back into the original array
            for (int x = 0; x < realxdim; x++) {
                for (int y = 0; y < realydim; y++) {
                    for (int z = 0; z < realzdim; z++) {
                        rank[x + y * realxdim + z * realydim * realxdim] = rankh[x + y * xdim + z * ydim * xdim];
                    }
                }
            }
            int* exists = new int[xdim * ydim * zdim];
            for (int x = 0; x < xdim * ydim * zdim; x++) {
                exists[x] = 0;
            }

            for (int x = 0; x < realxdim * realydim * realzdim; x++) {
                exists[rank[x]] = 1;
            }
            for (int x = 1; x < xdim * ydim * zdim; x++) {
                exists[x] = exists[x-1] + exists[x];
            }
            for (int x = 0; x < realxdim * realydim * realzdim; x++) {
                rank[x] = exists[rank[x]]-1;
            }
            //set dimensions back to non-rounded form
            xdim = realxdim;
            ydim = realydim;
            zdim = realzdim;

            if (DEBUG) {
                //print out the points we assigned by rank
                //(can't really print out a 3D map like we did for 2D)
                printf("resulting indices, in order:\n");
                bool flag;
                for (int rankin = 0; rankin < xdim * ydim * zdim; rankin++) {
                    //find the point
                    flag = false;
                    for (int index = 0; index < xdim * ydim * zdim; index++) {
                        if(rank[index] == rankin) {
                            if (flag) {
                                printf("error: double");
                                exit(1);
                            }
                            printf("(%d,%d,%d),  ", index % xdim, (index % (xdim * ydim)) / xdim, index / (xdim * ydim));
                            if ((rankin + 1) % xdim == 0) printf("\n");

                            flag = true;
                        }
                    }
                    if (!flag) {
                        printf("error: %d not found", rankin);
                        exit(1);
                    }
                } 
            }
            //int* exists = new int[xdim * ydim * zdim];
            //for(int x = 0; x < xdim * ydim * zdim; x++)
            //    exists[x] = 0;
            //for(int x = 0; x < xdim * ydim * zdim; x++)
            //    exists[rank[x]]++;
            //for(int x = 0; x < xdim * ydim * zdim; x++)
            //    if(exists[x] != 1)
            //        printf("ERROR: %d %d", x, exists[x]);
            //printf("Done listing errors\n");
            //exit(1);
            delete [] exists;
            delete [] rot;
            delete [] next;
            delete [] mirrors;
            delete [] inverse;
        }
    } else {
        //snake curve
        rank = new int[xdim * ydim * zdim];
        int xcount = 0;
        int ycount = 0; 
        int zcount = 0;
        int xdir = 1;
        int ydir = 1;
        int totalcount = 0;
        while (zcount < zdim) {
            rank[xcount + ycount*xdim + zcount*ydim*xdim] = totalcount;
            totalcount++;
            if (xcount + xdir >= xdim || xcount + xdir < 0) {
                xdir *= -1;
                if (ycount + ydir >= ydim || ycount + ydir < 0) {
                    ydir *= -1;
                    zcount++; //z always goes up; once it reaches its max we're done
                } else {
                    ycount+= ydir;
                }
            } else {
                xcount += xdir;
            }
        }
    }
}

int LinearAllocator::MeshLocationOrdering::rankOf(MeshLocation* L) 
{
    //returns the rank of a given location
    //have to mix which coordinate is which in case x was not smallest
    int coordinates[3];
    coordinates[xpos] = L->x;
    coordinates[ypos] = L->y;
    coordinates[zpos] = L->z;
    //printf("returning %d", rank[coordinates[0] + coordinates[1] * xdim + coordinates[2] * xdim * ydim]);
    return rank[coordinates[0] + coordinates[1] * xdim + coordinates[2] * xdim * ydim];
}

/*
   currently unused (and doesn't work with dimordering)
   MeshLocation* LinearAllocator::MeshLocationOrdering::locationOf(int Rank) {
//return MeshLocation having given rank
//raises exception if Rank is out of range

int pos = -1;   //ordinal value of position having rank
int x, y, z;    //coordinate values

//find Rank in array with ordering
int i=0;
while(pos == -1) {
if(rank[i] == Rank)
pos = i;
i++;
}

//translate position in ordering into coordinates
x = pos % xdim;
pos = pos / xdim;
y = pos % ydim;
z = pos / ydim;
return new MeshLocation(x, y, z);
}
*/

LinearAllocator::LinearAllocator(vector<string>* params, Machine* mach) 
{
    //takes machine to be allocated and name of the file with ordering
    //file format is as described in comment at top of this file
    MachineMesh* m = dynamic_cast<MachineMesh*>(mach);
    if (NULL == m) {
        error("Linear allocators require a MachineMesh* machine");
    }

    machine = m;

    bool sort, hilbert;
    sort = false;
    hilbert = false;
    switch (params -> size()) {
    case 0:
        break;
    case 1:
        if ("sort" == params -> at(0)) {
            sort = true;
        } else if ("nosort" == params -> at(0)) {
            sort = false;
        } else if ("hilbert" == params -> at (0)) {
            hilbert = true;
        } else if ("snake" == params -> at (0)) {
            hilbert = false;
        } else {
            error("Argument to Linear Allocator must be sort, nosort, hilbert, or snake:" + params -> at(0));
        }
        break;
    case 2:
        if ("sort" == params -> at(0)) {
            sort = true;
        } else if ("nosort" == params -> at(0)) {
            sort = false;
        } else {
            error("First argument to Linear Allocator must be sort or nosort:" + params -> at(0));
        }
        if ("hilbert" == params -> at (1)) {
            hilbert = true;
        } else if ("snake" == params -> at (1)) {
            hilbert = false;
        } else {
            error("Second argument to Linear Allocator must be hilbert or snake:" + params -> at(1));
        }
        break;
    }
    ordering = new MeshLocationOrdering(m, sort, hilbert);
}

//returns list of intervals of free processors
//each interval represented by a list of its locations
vector<vector<MeshLocation*>*>* LinearAllocator::getIntervals() {

    set<MeshLocation*, MeshLocationOrdering>* avail = new set<MeshLocation*,MeshLocationOrdering>(*ordering);
    //add all from machine->freeProcessors() to avail
    vector<MeshLocation*>* machfree = ((MachineMesh*)machine) -> freeProcessors();

    avail -> insert(machfree -> begin(), machfree -> end());

    vector<vector<MeshLocation*>*>* retVal =  //list of intervals so far
        new vector<vector<MeshLocation*>*>();

    vector<MeshLocation*>* curr =               //interval being built
        new vector<MeshLocation*>();

    int lastRank = -2;                           //rank of last element
    //-2 is sentinel value

    for (set<MeshLocation*,MeshLocationOrdering>::iterator ml = avail -> begin(); ml != avail -> end(); ml++) {
        int mlRank = ordering -> rankOf(*ml);
        if ((mlRank != lastRank + 1) && (lastRank != -2)) {
            //need to start new interval
            retVal -> push_back(curr);
            curr = new vector<MeshLocation*>();
        }
        curr->push_back(*ml);
        lastRank = mlRank;
    }
    if (curr -> size() != 0) {  //add last interval if nonempty
        retVal -> push_back(curr);
    } else {
        curr -> clear();
        delete curr;
    }

    if (DEBUG) {
        printf("getIntervals:");
        for (vector<vector<MeshLocation*>*>::iterator ar = retVal -> begin(); ar != retVal -> end(); ar++) {
            printf("Interval: ");
            for (int x = 0; x < (int)(*ar) -> size(); x++) {
                (*ar) -> at(x) -> print();
                printf(" ");
            }
            printf("\n");
        }
    }
    avail -> clear();
    machfree -> clear();
    delete avail;
    delete machfree;

    return retVal;
}

//Version of allocate that just minimizes the span.
AllocInfo* LinearAllocator::minSpanAllocate(Job* job) {

    vector<MeshLocation*>* avail = ((MachineMesh*)machine) -> freeProcessors();
    sort(avail -> begin(), avail -> end(), *ordering);
    int num = job -> getProcsNeeded();

    //scan through possible starting locations to find best one
    int bestStart = 0;   //index of best starting location so far
    int bestSpan = ordering -> rankOf(avail -> at(num -1)) - ordering-> rankOf(avail -> at(0));//best location's span
    for (int i = 1; i <= (int)avail -> size() - num; i++) {
        int candidate = ordering -> rankOf(avail -> at(i+num -1)) - ordering-> rankOf(avail -> at(i));
        if (candidate < bestSpan) {
            bestStart = i;
            bestSpan = candidate;
        }
    }

    //return the best allocation found
    MeshAllocInfo* retVal = new MeshAllocInfo(job);
    for (int i = 0; i< (int)avail -> size(); i++) {
        if (i >= bestStart && i < bestStart + num) {
            retVal -> processors -> at(i  - bestStart) = avail-> at(i);
            retVal -> nodeIndices[i  - bestStart] = avail-> at(i)->toInt((MachineMesh*)machine);
        } else {
            delete avail -> at(i); //have to delete any not being used
        }
    }
    avail -> clear();
    delete avail;
    return retVal;
}
