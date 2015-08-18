/*
 *@BEGIN LICENSE
 *
 * PSI4: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * BP-v2RDM: a boundary-point semidefinite solver for variational 2-RDM
 *           computations.
 *
 * Copyright (c) 2014, The Florida State University. All rights reserved.
 * 
 *@END LICENSE
 *
 * This code performs a semidefinite optimization of the electronic
 * energy according to the boundary-point algorithm described in
 * PRL 106, 083001 (2011).
 *
 */
#include <psi4-dec.h>
#include <libparallel/parallel.h>
#include <liboptions/liboptions.h>
#include <libqt/qt.h>

#include<libtrans/integraltransform.h>
#include<libtrans/mospace.h>

#include<libmints/wavefunction.h>
#include<libmints/mints.h>
#include<libmints/vector.h>
#include<libmints/matrix.h>
#include<../bin/fnocc/blas.h>
#include<time.h>

#include"v2rdm_solver.h"

#ifdef _OPENMP
    #include<omp.h>
#else
    #define omp_get_wtime() ( (double)clock() / CLOCKS_PER_SEC )
    #define omp_get_max_threads() 1
#endif

using namespace boost;
using namespace psi;
using namespace fnocc;

namespace psi{ namespace v2rdm_casscf{


void v2RDMSolver::BuildBasis() {

    // product table:
    table = (int*)malloc(64*sizeof(int));
    memset((void*)table,'\0',64*sizeof(int));
    table[0*8+1] = table[1*8+0] = 1;
    table[0*8+2] = table[2*8+0] = 2;
    table[0*8+3] = table[3*8+0] = 3;
    table[0*8+4] = table[4*8+0] = 4;
    table[0*8+5] = table[5*8+0] = 5;
    table[0*8+6] = table[6*8+0] = 6;
    table[0*8+7] = table[7*8+0] = 7;
    table[1*8+2] = table[2*8+1] = 3;
    table[1*8+3] = table[3*8+1] = 2;
    table[1*8+4] = table[4*8+1] = 5;
    table[1*8+5] = table[5*8+1] = 4;
    table[1*8+6] = table[6*8+1] = 7;
    table[1*8+7] = table[7*8+1] = 6;
    table[2*8+3] = table[3*8+2] = 1;
    table[2*8+4] = table[4*8+2] = 6;
    table[2*8+5] = table[5*8+2] = 7;
    table[2*8+6] = table[6*8+2] = 4;
    table[2*8+7] = table[7*8+2] = 5;
    table[3*8+4] = table[4*8+3] = 7;
    table[3*8+5] = table[5*8+3] = 6;
    table[3*8+6] = table[6*8+3] = 5;
    table[3*8+7] = table[7*8+3] = 4;
    table[4*8+5] = table[5*8+4] = 1;
    table[4*8+6] = table[6*8+4] = 2;
    table[4*8+7] = table[7*8+4] = 3;
    table[5*8+6] = table[6*8+5] = 3;
    table[5*8+7] = table[7*8+5] = 2;
    table[6*8+7] = table[7*8+6] = 1;

    // orbitals are in pitzer order:
    symmetry               = (int*)malloc((nmo+nfrzc+nfrzv)*sizeof(int));
    symmetry_full          = (int*)malloc((nmo+nfrzc+nfrzv)*sizeof(int));
    symmetry_plus_core     = (int*)malloc((nmo+nfrzc)*sizeof(int));
    symmetry_energy_order  = (int*)malloc((nmo+nfrzc+nfrzv)*sizeof(int));
    pitzer_to_energy_order = (int*)malloc((nmo+nfrzc+nfrzv)*sizeof(int));
    energy_to_pitzer_order = (int*)malloc((nmo+nfrzc+nfrzv)*sizeof(int));

    memset((void*)symmetry,'\0',(nmo+nfrzc+nfrzv)*sizeof(int));
    memset((void*)symmetry_full,'\0',(nmo+nfrzc+nfrzv)*sizeof(int));
    memset((void*)symmetry_plus_core,'\0',(nmo+nfrzc)*sizeof(int));
    memset((void*)symmetry_energy_order,'\0',(nmo+nfrzc+nfrzv)*sizeof(int));
    memset((void*)pitzer_to_energy_order,'\0',(nmo+nfrzc+nfrzv)*sizeof(int));
    memset((void*)energy_to_pitzer_order,'\0',(nmo+nfrzc+nfrzv)*sizeof(int));
    full_basis = (int*)malloc((nmo+nfrzc+nfrzv)*sizeof(int));
    int count = 0;
    int count_full = 0;

    // symmetry of ACTIVE orbitals
    for (int h = 0; h < nirrep_; h++) {
        count_full += frzcpi_[h];
        for (int norb = frzcpi_[h]; norb < nmopi_[h] - frzvpi_[h]; norb++){
            full_basis[count] = count_full++;
            symmetry[count++] = h;
        }
        count_full += frzvpi_[h];
    }

    // symmetry of ALL orbitals
    count = 0;
    for (int h = 0; h < nirrep_; h++) {
        for (int norb = 0; norb < nmopi_[h]; norb++){
            symmetry_full[count++] = h;
        }
    }
    // symmetry of ALL orbitals, except frozen virtual
    count = 0;
    for (int h = 0; h < nirrep_; h++) {
        for (int norb = 0; norb < amopi_[h] + frzcpi_[h]; norb++){
            symmetry_plus_core[count++] = h;
        }
    }
    // symmetry of ALL orbitals in energy order
    double min = 1.0e99;
    int imin = -999;
    int isym = -999;
    int * skip = (int*)malloc((nmo+nfrzc+nfrzv)*sizeof(int));
    memset((void*)skip,'\0',(nmo+nfrzc+nfrzv)*sizeof(int));

    // warning to future eugene:  it is possible that
    // this ordering will differ from that printed at the
    // end of the SCF routine if orbitals are truly
    // degenerate.  past eugene hasn't convinved himself
    // of whether or not this is actually a problem.

    // TODO: the orbital ordering should be according to 
    // energy within each type of orbital

    // core
    for (int i = 0; i < nfrzc; i++){

        int me = 0;
        min = 1.0e99;
        for (int h = 0; h < nirrep_; h++) {
            for (int j = 0; j < frzcpi_[h]; j++){
                if ( epsilon_a_->pointer(h)[j] < min ) {
                    if ( !skip[me] ) {
                        min = epsilon_a_->pointer(h)[j];
                        imin = me;
                        isym = h;
                    }
                }
                me++;
            }
            me += nmopi_[h] - frzcpi_[h];
        }
        skip[imin] = 1;
        symmetry_energy_order[i] = isym + 1;
        pitzer_to_energy_order[imin] = i;
        energy_to_pitzer_order[i] = imin;
    }
    // active
    for (int i = nfrzc; i < nmo + nfrzc; i++){

        int me = 0;
        min = 1.0e99;
        for (int h = 0; h < nirrep_; h++) {
            me += frzcpi_[h];
            for (int j = frzcpi_[h]; j < frzcpi_[h]+amopi_[h]; j++){
                if ( epsilon_a_->pointer(h)[j] < min ) {
                    if ( !skip[me] ) {
                        min = epsilon_a_->pointer(h)[j];
                        imin = me;
                        isym = h;
                    }
                }
                me++;
            }
            me += frzvpi_[h];
        }
        skip[imin] = 1;
        symmetry_energy_order[i] = isym + 1;
        pitzer_to_energy_order[imin] = i;
        energy_to_pitzer_order[i] = imin;
    }
    // virtual
    for (int i = nmo + nfrzc; i < nfrzv + nmo + nfrzc; i++){

        int me = 0;
        min = 1.0e99;
        for (int h = 0; h < nirrep_; h++) {
            me += frzcpi_[h] + amopi_[h];
            for (int j = frzcpi_[h] + amopi_[h]; j < nmopi_[h]; j++){
                if ( epsilon_a_->pointer(h)[j] < min ) {
                    if ( !skip[me] ) {
                        min = epsilon_a_->pointer(h)[j];
                        imin = me;
                        isym = h;
                    }
                }
                me++;
            }
        }
        skip[imin] = 1;
        symmetry_energy_order[i] = isym + 1;
        pitzer_to_energy_order[imin] = i;
        energy_to_pitzer_order[i] = imin;
    }

    pitzer_offset           = (int*)malloc(nirrep_*sizeof(int));
    pitzer_offset_full      = (int*)malloc(nirrep_*sizeof(int));
    pitzer_offset_plus_core = (int*)malloc(nirrep_*sizeof(int));
    count = 0;
    for (int h = 0; h < nirrep_; h++) {
        pitzer_offset[h] = count;
        count += nmopi_[h] - frzcpi_[h] - frzvpi_[h];
    }
    count = 0;
    for (int h = 0; h < nirrep_; h++) {
        pitzer_offset_full[h] = count;
        count += nmopi_[h];
    }
    count = 0;
    for (int h = 0; h < nirrep_; h++) {
        pitzer_offset_plus_core[h] = count;
        count += nmopi_[h] - frzvpi_[h];
    }
    // symmetry pairs:
    int ** sympairs = (int**)malloc(nirrep_*sizeof(int*));
    for (int h = 0; h < nirrep_; h++) {
        sympairs[h] = (int*)malloc(nmo*sizeof(int));
        memset((void*)sympairs[h],'\0',nmo*sizeof(int));
    }

    for (int h = 0; h < nirrep_; h++) {
        std::vector < std::pair<int,int> > mygems;
        for (int i = 0; i < nmo; i++) {
            for (int j = 0; j < nmo; j++) {
                int sym = SymmetryPair(symmetry[i],symmetry[j]);
                if (h==sym) {
                    mygems.push_back(std::make_pair(j,i));
                }

            }
        }
        gems.push_back(mygems);
    }
    for (int h = 0; h < nirrep_; h++) {
        std::vector < std::pair<int,int> > mygems;
        for (int i = 0; i < nmo + nfrzc + nfrzv; i++) {
            for (int j = 0; j <= i; j++) {
                int sym = SymmetryPair(symmetry_full[i],symmetry_full[j]);
                if (h==sym) {
                    mygems.push_back(std::make_pair(i,j));
                }

            }
        }
        gems_fullspace.push_back(mygems);
    }
    for (int h = 0; h < nirrep_; h++) {
        std::vector < std::pair<int,int> > mygems;
        for (int i = 0; i < nmo + nfrzc; i++) {
            for (int j = 0; j <= i; j++) {
                int sym = SymmetryPair(symmetry_plus_core[i],symmetry_plus_core[j]);
                if (h==sym) {
                    mygems.push_back(std::make_pair(i,j));
                }

            }
        }
        gems_plus_corespace.push_back(mygems);
    }

    bas_ab_sym         = (int***)malloc(nirrep_*sizeof(int**));
    bas_aa_sym         = (int***)malloc(nirrep_*sizeof(int**));
    bas_00_sym         = (int***)malloc(nirrep_*sizeof(int**));
    bas_full_sym       = (int***)malloc(nirrep_*sizeof(int**));

    ibas_ab_sym        = (int***)malloc(nirrep_*sizeof(int**));
    ibas_aa_sym        = (int***)malloc(nirrep_*sizeof(int**));
    ibas_00_sym        = (int***)malloc(nirrep_*sizeof(int**));
    ibas_full_sym      = (int***)malloc(nirrep_*sizeof(int**));

    gems_ab            = (int*)malloc(nirrep_*sizeof(int));
    gems_aa            = (int*)malloc(nirrep_*sizeof(int));
    gems_00            = (int*)malloc(nirrep_*sizeof(int));
    gems_full          = (int*)malloc(nirrep_*sizeof(int));
    gems_plus_core     = (int*)malloc(nirrep_*sizeof(int));

    for (int h = 0; h < nirrep_; h++) {

        ibas_ab_sym[h]        = (int**)malloc(nmo*sizeof(int*));
        ibas_aa_sym[h]        = (int**)malloc(nmo*sizeof(int*));
        ibas_00_sym[h]        = (int**)malloc(nmo*sizeof(int*));
        ibas_full_sym[h]      = (int**)malloc((nmo+nfrzc+nfrzv)*sizeof(int*));

        bas_ab_sym[h]         = (int**)malloc(nmo*nmo*sizeof(int*));
        bas_aa_sym[h]         = (int**)malloc(nmo*nmo*sizeof(int*));
        bas_00_sym[h]         = (int**)malloc(nmo*nmo*sizeof(int*));
        bas_full_sym[h]       = (int**)malloc((nmo+nfrzc+nfrzv)*(nmo+nfrzc+nfrzv)*sizeof(int*));

        // active space geminals
        for (int i = 0; i < nmo; i++) {
            ibas_ab_sym[h][i] = (int*)malloc(nmo*sizeof(int));
            ibas_aa_sym[h][i] = (int*)malloc(nmo*sizeof(int));
            ibas_00_sym[h][i] = (int*)malloc(nmo*sizeof(int));
            for (int j = 0; j < nmo; j++) {
                ibas_ab_sym[h][i][j] = -999;
                ibas_aa_sym[h][i][j] = -999;
                ibas_00_sym[h][i][j] = -999;
            }
        }
        for (int i = 0; i < nmo*nmo; i++) {
            bas_ab_sym[h][i] = (int*)malloc(2*sizeof(int));
            bas_aa_sym[h][i] = (int*)malloc(2*sizeof(int));
            bas_00_sym[h][i] = (int*)malloc(2*sizeof(int));
            for (int j = 0; j < 2; j++) {
                bas_ab_sym[h][i][j] = -999;
                bas_aa_sym[h][i][j] = -999;
                bas_00_sym[h][i][j] = -999;
            }
        }
        // full space geminals
        for (int i = 0; i < nmo+nfrzv+nfrzc; i++) {
            ibas_full_sym[h][i] = (int*)malloc((nmo+nfrzc+nfrzv)*sizeof(int));
            for (int j = 0; j < nfrzv+nfrzc+nmo; j++) {
                ibas_full_sym[h][i][j] = -999;
            }
        }
        for (int i = 0; i < (nfrzc+nfrzv+nmo)*(nfrzc+nfrzv+nmo); i++) {
            bas_full_sym[h][i] = (int*)malloc(2*sizeof(int));
            for (int j = 0; j < 2; j++) {
                bas_full_sym[h][i][j] = -999;
            }
        }

        // active space mappings:
        int count_ab = 0;
        int count_aa = 0;
        int count_00 = 0;
        for (int n = 0; n < gems[h].size(); n++) {
            int i = gems[h][n].first;
            int j = gems[h][n].second;

            ibas_ab_sym[h][i][j] = n;
            bas_ab_sym[h][n][0]  = i;
            bas_ab_sym[h][n][1]  = j;
            count_ab++;

            if ( i < j ) continue;

            ibas_00_sym[h][i][j] = count_00;
            ibas_00_sym[h][j][i] = count_00;
            bas_00_sym[h][count_00][0] = i;
            bas_00_sym[h][count_00][1] = j;
            count_00++;

            if ( i <= j ) continue;

            ibas_aa_sym[h][i][j] = count_aa;
            ibas_aa_sym[h][j][i] = count_aa;
            bas_aa_sym[h][count_aa][0] = i;
            bas_aa_sym[h][count_aa][1] = j;
            count_aa++;
        }
        gems_ab[h] = count_ab;
        gems_aa[h] = count_aa;
        gems_00[h] = count_00;

    }

    // new way:
    memset((void*)gems_full,'\0',nirrep_*sizeof(int));
    memset((void*)gems_plus_core,'\0',nirrep_*sizeof(int));

    for (int ieo = 0; ieo < nmo + nfrzc + nfrzv; ieo++) {
        int ifull = energy_to_pitzer_order[ieo];
        int hi    = symmetry_full[ifull];
        int i     = ifull - pitzer_offset_full[hi];
        for (int jeo = 0; jeo <= ieo; jeo++) {
            int jfull = energy_to_pitzer_order[jeo];
            int hj    = symmetry_full[jfull];
            int j     = jfull - pitzer_offset_full[hj];

            int hij = SymmetryPair(hi,hj);
            ibas_full_sym[hij][ifull][jfull] = gems_full[hij];
            ibas_full_sym[hij][jfull][ifull] = gems_full[hij];
            bas_full_sym[hij][gems_full[hij]][0] = ifull;
            bas_full_sym[hij][gems_full[hij]][1] = jfull;
            gems_full[hij]++;
            if ( ieo < nmo + nfrzc && jeo < nmo + nfrzc ) {
                gems_plus_core[hij]++;
            }
        }
    }
    if ( constrain_t1 || constrain_t2 || constrain_d3 ) {
        // make all triplets
        for (int h = 0; h < nirrep_; h++) {
            std::vector < boost::tuple<int,int,int> > mytrip;
            for (int i = 0; i < nmo; i++) {
                for (int j = 0; j < nmo; j++) {
                    int s1 = SymmetryPair(symmetry[i],symmetry[j]);
                    for (int k = 0; k < nmo; k++) {
                        int s2 = SymmetryPair(s1,symmetry[k]);
                        if (h==s2) {
                            mytrip.push_back(boost::make_tuple(i,j,k));
                        }
                    }

                }
            }
            triplets.push_back(mytrip);
        }
        bas_aaa_sym  = (int***)malloc(nirrep_*sizeof(int**));
        bas_aab_sym  = (int***)malloc(nirrep_*sizeof(int**));
        bas_aba_sym  = (int***)malloc(nirrep_*sizeof(int**));
        ibas_aaa_sym = (int****)malloc(nirrep_*sizeof(int***));
        ibas_aab_sym = (int****)malloc(nirrep_*sizeof(int***));
        ibas_aba_sym = (int****)malloc(nirrep_*sizeof(int***));
        trip_aaa    = (int*)malloc(nirrep_*sizeof(int));
        trip_aab    = (int*)malloc(nirrep_*sizeof(int));
        trip_aba    = (int*)malloc(nirrep_*sizeof(int));
        for (int h = 0; h < nirrep_; h++) {
            ibas_aaa_sym[h] = (int***)malloc(nmo*sizeof(int**));
            ibas_aab_sym[h] = (int***)malloc(nmo*sizeof(int**));
            ibas_aba_sym[h] = (int***)malloc(nmo*sizeof(int**));
            bas_aaa_sym[h]  = (int**)malloc(nmo*nmo*nmo*sizeof(int*));
            bas_aab_sym[h]  = (int**)malloc(nmo*nmo*nmo*sizeof(int*));
            bas_aba_sym[h]  = (int**)malloc(nmo*nmo*nmo*sizeof(int*));
            for (int i = 0; i < nmo; i++) {
                ibas_aaa_sym[h][i] = (int**)malloc(nmo*sizeof(int*));
                ibas_aab_sym[h][i] = (int**)malloc(nmo*sizeof(int*));
                ibas_aba_sym[h][i] = (int**)malloc(nmo*sizeof(int*));
                for (int j = 0; j < nmo; j++) {
                    ibas_aaa_sym[h][i][j] = (int*)malloc(nmo*sizeof(int));
                    ibas_aab_sym[h][i][j] = (int*)malloc(nmo*sizeof(int));
                    ibas_aba_sym[h][i][j] = (int*)malloc(nmo*sizeof(int));
                    for (int k = 0; k < nmo; k++) {
                        ibas_aaa_sym[h][i][j][k] = -999;
                        ibas_aab_sym[h][i][j][k] = -999;
                        ibas_aba_sym[h][i][j][k] = -999;
                    }
                }
            }
            for (int i = 0; i < nmo*nmo*nmo; i++) {
                bas_aaa_sym[h][i] = (int*)malloc(3*sizeof(int));
                bas_aab_sym[h][i] = (int*)malloc(3*sizeof(int));
                bas_aba_sym[h][i] = (int*)malloc(3*sizeof(int));
                for (int j = 0; j < 3; j++) {
                    bas_aaa_sym[h][i][j] = -999;
                    bas_aab_sym[h][i][j] = -999;
                    bas_aba_sym[h][i][j] = -999;
                }
            }

            // mappings:
            int count_aaa = 0;
            int count_aab = 0;
            int count_aba = 0;
            for (int n = 0; n < triplets[h].size(); n++) {
                int i = get<0>(triplets[h][n]);
                int j = get<1>(triplets[h][n]);
                int k = get<2>(triplets[h][n]);

                ibas_aba_sym[h][i][j][k] = count_aba;
                bas_aba_sym[h][count_aba][0]  = i;
                bas_aba_sym[h][count_aba][1]  = j;
                bas_aba_sym[h][count_aba][2]  = k;
                count_aba++;

                if ( i >= j ) continue;

                ibas_aab_sym[h][i][j][k] = count_aab;
                ibas_aab_sym[h][j][i][k] = count_aab;
                bas_aab_sym[h][count_aab][0]  = i;
                bas_aab_sym[h][count_aab][1]  = j;
                bas_aab_sym[h][count_aab][2]  = k;
                count_aab++;

                if ( j >= k ) continue;

                ibas_aaa_sym[h][i][j][k] = count_aaa;
                ibas_aaa_sym[h][i][k][j] = count_aaa;
                ibas_aaa_sym[h][j][i][k] = count_aaa;
                ibas_aaa_sym[h][j][k][i] = count_aaa;
                ibas_aaa_sym[h][k][i][j] = count_aaa;
                ibas_aaa_sym[h][k][j][i] = count_aaa;
                bas_aaa_sym[h][count_aaa][0]  = i;
                bas_aaa_sym[h][count_aaa][1]  = j;
                bas_aaa_sym[h][count_aaa][2]  = k;
                count_aaa++;

            }
            trip_aaa[h] = count_aaa;
            trip_aab[h] = count_aab;
            trip_aba[h] = count_aba;
        }
    }
}


}}