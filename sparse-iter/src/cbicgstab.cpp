/*
    -- MAGMA (version 1.6.1) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       @date January 2015

       @generated from zbicgstab.cpp normal z -> c, Fri Jan 30 19:00:30 2015
       @author Hartwig Anzt

*/
#include "common_magma.h"
#include "magmasparse.h"

#include <assert.h>


#define RTOLERANCE     lapackf77_slamch( "E" )
#define ATOLERANCE     lapackf77_slamch( "E" )


/**
    Purpose
    -------

    Solves a system of linear equations
       A * X = B
    where A is a complex Hermitian N-by-N positive definite matrix A.
    This is a GPU implementation of the Biconjugate Gradient Stabelized method.

    Arguments
    ---------

    @param[in]
    A           magma_c_sparse_matrix
                input matrix A

    @param[in]
    b           magma_c_vector
                RHS b

    @param[in,out]
    x           magma_c_vector*
                solution approximation

    @param[in,out]
    solver_par  magma_c_solver_par*
                solver parameters
    @param[in]
    queue       magma_queue_t
                Queue to execute in.

    @ingroup magmasparse_cgesv
    ********************************************************************/

extern "C" magma_int_t
magma_cbicgstab(
    magma_c_sparse_matrix A, magma_c_vector b, magma_c_vector *x,  
    magma_c_solver_par *solver_par,
    magma_queue_t queue )
{
    // set queue for old dense routines
    magma_queue_t orig_queue;
    magmablasGetKernelStream( &orig_queue );

    // prepare solver feedback
    solver_par->solver = Magma_BICGSTAB;
    solver_par->numiter = 0;
    solver_par->info = MAGMA_SUCCESS;

    // some useful variables
    magmaFloatComplex c_zero = MAGMA_C_ZERO, c_one = MAGMA_C_ONE, 
                                            c_mone = MAGMA_C_NEG_ONE;
    
    magma_int_t dofs = A.num_rows;

    // workspace
    magma_c_vector r,rr,p,v,s,t;
    magma_c_vinit( &r, Magma_DEV, dofs, c_zero, queue );
    magma_c_vinit( &rr, Magma_DEV, dofs, c_zero, queue );
    magma_c_vinit( &p, Magma_DEV, dofs, c_zero, queue );
    magma_c_vinit( &v, Magma_DEV, dofs, c_zero, queue );
    magma_c_vinit( &s, Magma_DEV, dofs, c_zero, queue );
    magma_c_vinit( &t, Magma_DEV, dofs, c_zero, queue );

    
    // solver variables
    magmaFloatComplex alpha, beta, omega, rho_old, rho_new;
    float nom, betanom, nom0, r0, den, res;

    // solver setup
    magma_cscal( dofs, c_zero, x->dval, 1) ;                    // x = 0
    magma_ccopy( dofs, b.dval, 1, r.dval, 1 );                   // r = b
    magma_ccopy( dofs, b.dval, 1, rr.dval, 1 );                  // rr = b
    nom0 = betanom = magma_scnrm2( dofs, r.dval, 1 );           // nom = || r ||
    nom = nom0*nom0;
    rho_old = omega = alpha = MAGMA_C_MAKE( 1.0, 0. );
    solver_par->init_res = nom0;

    magma_c_spmv( c_one, A, r, c_zero, v, queue );                      // z = A r
    den = MAGMA_C_REAL( magma_cdotc(dofs, v.dval, 1, r.dval, 1) ); // den = z' * r

    if ( (r0 = nom * solver_par->epsilon) < ATOLERANCE ) 
        r0 = ATOLERANCE;
    if ( nom < r0 ) {
        magmablasSetKernelStream( orig_queue );
        return MAGMA_SUCCESS;
    }
    // check positive definite  
    if (den <= 0.0) {
        printf("Operator A is not postive definite. (Ar,r) = %f\n", den);
        magmablasSetKernelStream( orig_queue );
        return MAGMA_NONSPD;
        solver_par->info = MAGMA_NONSPD;;
    }

    //Chronometry
    real_Double_t tempo1, tempo2;
    tempo1 = magma_sync_wtime( queue );
    if ( solver_par->verbose > 0 ) {
        solver_par->res_vec[0] = nom0;
        solver_par->timing[0] = 0.0;
    }

    // start iteration
    for( solver_par->numiter= 1; solver_par->numiter<solver_par->maxiter; 
                                                    solver_par->numiter++ ) {

        rho_new = magma_cdotc( dofs, rr.dval, 1, r.dval, 1 );  // rho=<rr,r>
        beta = rho_new/rho_old * alpha/omega;   // beta=rho/rho_old *alpha/omega
        magma_cscal( dofs, beta, p.dval, 1 );                 // p = beta*p
        magma_caxpy( dofs, c_mone * omega * beta, v.dval, 1 , p.dval, 1 );        
                                                        // p = p-omega*beta*v
        magma_caxpy( dofs, c_one, r.dval, 1, p.dval, 1 );      // p = p+r
        magma_c_spmv( c_one, A, p, c_zero, v, queue );              // v = Ap

        alpha = rho_new / magma_cdotc( dofs, rr.dval, 1, v.dval, 1 );
        magma_ccopy( dofs, r.dval, 1 , s.dval, 1 );            // s=r
        magma_caxpy( dofs, c_mone * alpha, v.dval, 1 , s.dval, 1 ); // s=s-alpha*v

        magma_c_spmv( c_one, A, s, c_zero, t, queue );               // t=As
        omega = magma_cdotc( dofs, t.dval, 1, s.dval, 1 )   // omega = <s,t>/<t,t>
                   / magma_cdotc( dofs, t.dval, 1, t.dval, 1 );

        magma_caxpy( dofs, alpha, p.dval, 1 , x->dval, 1 );     // x=x+alpha*p
        magma_caxpy( dofs, omega, s.dval, 1 , x->dval, 1 );     // x=x+omega*s

        magma_ccopy( dofs, s.dval, 1 , r.dval, 1 );             // r=s
        magma_caxpy( dofs, c_mone * omega, t.dval, 1 , r.dval, 1 ); // r=r-omega*t
        res = betanom = magma_scnrm2( dofs, r.dval, 1 );

        nom = betanom*betanom;
        rho_old = rho_new;                                    // rho_old=rho

        if ( solver_par->verbose > 0 ) {
            tempo2 = magma_sync_wtime( queue );
            if ( (solver_par->numiter)%solver_par->verbose==0 ) {
                solver_par->res_vec[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) res;
                solver_par->timing[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) tempo2-tempo1;
            }
        }

        if ( res/nom0  < solver_par->epsilon ) {
            break;
        }
    }
    tempo2 = magma_sync_wtime( queue );
    solver_par->runtime = (real_Double_t) tempo2-tempo1;
    float residual;
    magma_cresidual( A, b, *x, &residual, queue );
    solver_par->iter_res = res;
    solver_par->final_res = residual;

    if ( solver_par->numiter < solver_par->maxiter) {
        solver_par->info = MAGMA_SUCCESS;
    } else if ( solver_par->init_res > solver_par->final_res ) {
        if ( solver_par->verbose > 0 ) {
            if ( (solver_par->numiter)%solver_par->verbose==0 ) {
                solver_par->res_vec[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) betanom;
                solver_par->timing[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) tempo2-tempo1;
            }
        }
        solver_par->info = MAGMA_SLOW_CONVERGENCE;
    }
    else {
        if ( solver_par->verbose > 0 ) {
            if ( (solver_par->numiter)%solver_par->verbose==0 ) {
                solver_par->res_vec[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) betanom;
                solver_par->timing[(solver_par->numiter)/solver_par->verbose] 
                        = (real_Double_t) tempo2-tempo1;
            }
        }
        solver_par->info = MAGMA_DIVERGENCE;
    }
    magma_c_vfree(&r, queue );
    magma_c_vfree(&rr, queue );
    magma_c_vfree(&p, queue );
    magma_c_vfree(&v, queue );
    magma_c_vfree(&s, queue );
    magma_c_vfree(&t, queue );

    magmablasSetKernelStream( orig_queue );
    return MAGMA_SUCCESS;
}   /* magma_cbicgstab */


