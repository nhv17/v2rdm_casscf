/*
 *@BEGIN LICENSE
 *
 * v2rdm_casscf by 
 *
 *   Jacob Fosso-Tande
 *   Greg Gidofalvi
 *   Eugene DePrince
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
 *@END LICENSE
 */

/*                                                                      
 *@BEGIN LICENSE

  Copyright (c) 2014, The Florida State University. All rights reserved.

 *@END LICENSE
 */


#include "v2rdm_solver.h"

#include <libplugin/plugin.h>
#include <psi4-dec.h>
#include <libparallel/parallel.h>
#include <liboptions/liboptions.h>
#include <libmints/mints.h>
#include <libpsio/psio.hpp>
#include<libciomr/libciomr.h>

INIT_PLUGIN

using namespace boost;
using namespace psi;
using namespace fnocc;

namespace psi{ namespace v2rdm_casscf {

extern "C" 
int read_options(std::string name, Options& options)
{
    if (name == "V2RDM_CASSCF"|| options.read_globals()) {
        /*- Do save progress in a checkpoint file? -*/
        options.add_bool("WRITE_CHECKPOINT_FILE",false);
        /*- Frequency of orbital optimization.  The checkpoint file is 
        updated every CHECKPOINT_FREQUENCY iterations.  The default frequency
        will be ORBOPT_FREQUENCY. -*/
        options.add_bool("CHECKPOINT_FREQUENCY",200);
        /*- File containing previous primal/dual solutions and integrals. -*/
        options.add_str("RESTART_FROM_CHECKPOINT_FILE","");
        /*- The type of 2-positivity computation -*/
        options.add_str("POSITIVITY", "DQG", "DQG D DQ DG DQGT1 DQGT2 DQGT1T2");
        /*- Do constrain D3 to D2 mapping? -*/
        options.add_bool("CONSTRAIN_D3",false);
        /*- Do spin adapt G2 condition? -*/
        options.add_bool("SPIN_ADAPT_G2", false);
        /*- Do spin adapt Q2 condition? -*/
        options.add_bool("SPIN_ADAPT_Q2", false);
        /*- Do constrain spin squared? -*/
        options.add_bool("CONSTRAIN_SPIN", true);
        /*- convergence in the primal/dual energy gap -*/
        options.add_double("E_CONVERGENCE", 1e-4);
        /*- convergence in the primal error -*/
        options.add_double("R_CONVERGENCE", 1e-3);
        /*- convergence for conjugate gradient solver. currently not used. -*/
        options.add_double("CG_CONVERGENCE", 1e-5);
        /*- maximum number of bpsdp outer iterations -*/
        options.add_int("MAXITER", 10000);
        /*- maximum number of conjugate gradient iterations -*/
        options.add_int("CG_MAXITER", 10000);

        /*- SUBSECTION SCF -*/

        /*- Auxiliary basis set for SCF density fitting computations.
        :ref:`Defaults <apdx:basisFamily>` to a JKFIT basis. -*/
        options.add_str("DF_BASIS_SCF", "");
        /*- What algorithm to use for the SCF computation. See Table :ref:`SCF
        Convergence & Algorithm <table:conv_scf>` for default algorithm for
        different calculation types. -*/
        options.add_str("SCF_TYPE", "DF", "DF CD PK OUT_OF_CORE");
        /*- Tolerance for Cholesky decomposition of the ERI tensor -*/
        options.add_double("CHOLESKY_TOLERANCE",1e-4);

        /*- SUBSECTION ORBITAL OPTIMIZATION -*/

        /*- flag to optimize orbitals using a one-step type approach -*/
        options.add_int("ORBOPT_ONE_STEP",1);
        /*- do rotate active/active orbital pairs? -*/
        options.add_bool("ORBOPT_ACTIVE_ACTIVE_ROTATIONS",false);
        /*- convergence in gradient norm -*/
        options.add_double("ORBOPT_GRADIENT_CONVERGENCE",1.0e-4);
        /*- convergence in energy for rotations -*/
        options.add_double("ORBOPT_ENERGY_CONVERGENCE",1.0e-8);
        /*- flag for using exact expresions for diagonal Hessian element -*/
        options.add_int("ORBOPT_EXACT_DIAGONAL_HESSIAN",0);
        /*- number of DIIS vectors to keep in orbital optimization -*/ 
        options.add_int("ORBOPT_NUM_DIIS_VECTORS",0);
        /*- frequency of orbital optimization.  optimization occurs every 
        orbopt_frequency iterations -*/
        options.add_int("ORBOPT_FREQUENCY",200);
        /*- Do write a MOLDEN output file?  If so, the filename will end in
        .molden, and the prefix is determined by |globals__writer_file_label|
        (if set), or else by the name of the output file plus the name of
        the current molecule. -*/
        options.add_bool("MOLDEN_WRITE", false);
        /*- Do write a ORBOPT output file?  If so, the filename will end in
        .molden, and the prefix is determined by |globals__writer_file_label|
        (if set), or else by the name of the output file plus the name of
        the current molecule. -*/
        options.add_bool("ORBOPT_WRITE", false);
        /*- Base filename for text files written by PSI, such as the
        MOLDEN output file, the Hessian file, the internal coordinate file,
        etc. Use the add_str_i function to make this string case sensitive. -*/
        options.add_str_i("WRITER_FILE_LABEL", "v2rdm_casscf");
    }

    return true;
}

extern "C" 
PsiReturnType v2rdm_casscf(Options& options)
{

    tstart();

    boost::shared_ptr<v2RDMSolver > v2rdm (new v2RDMSolver(Process::environment.wavefunction(),options));
    double energy = v2rdm->compute_energy();

    Process::environment.globals["CURRENT ENERGY"] = energy;

    tstop();

    return Success;
}

}} // End namespaces

