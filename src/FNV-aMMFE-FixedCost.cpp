//============================================================================
// Name        : FNV-aMMFE-FixedCost.cpp
// Author      : Tong WANG
// Email       : tong.wang@nus.edu.sg
// Version     : v3.0 (2013-04-16)
// Copyright   : ...
// Description : code for the Forecasting Newsvendor model with additive MMFE and fixed ordering costs
//============================================================================
// Require the Boost C++ Library (http://www.boost.org/)
// Compile: g++ -std=c++11 -fopenmp -O3 -I /usr/local/boost_1_53_0 -o FNV-aMMFE-FixedCost.exe FNV-aMMFE-FixedCost.cpp
//***********************************************************

#include <iostream>
#include <fstream>
#include <iomanip> //required by setprecision()
#include <cmath>
#include <numeric>
#include <vector>
#include <random>

#include <omp.h>

#include <boost/math/distributions/normal.hpp>

using namespace std;
using boost::math::normal;

//***********************************************************

#define N 3                             //Number of periods
#define STEP 100                        //discrete each standard deviation into STEP=100 segments
#define K 4                             //only consider +/- 4 standard deviations from the mean
#define RUN 10000000                    //Number of simulation runs

//***********************************************************
vector<double> phi(2*K*STEP+1);         //standard Normal pdf, calculated and saved in the vector $phi$

//Cost Parameters:
double r;                               //retail price
double lambda;                          //cost increment
vector<double> c(N+1);                  //vector for unit ordering costs, c(n) = c(1) + (n-1)*lambda

//Time Parameters:
double T;                               //timing of the last period;
vector<double> tau(N+2);                //vector for timing of the ordering periods, tau(n) = (n-1)/(N-1) * T

//Information Parameters:
double mu, var, stdev;                  //mean, variance, and stdev of the Brownian Motion
vector<double> stdev_epsilon(N+2);      //StDev of each additional piece of information

//Decisions
vector<double> b(N+1);                  //optimal safety stock levels in each period

vector<double> k(N+1), d(N+1);          //fixed ordering costs and the corresponding policy parameter

ofstream file;                          //output files

//***********************************************************
//g() function in the paper, first order derivative of G()
double g(int nn, double yy)
{
    double out=0;
    normal stdNormal;
    
    if (nn==N)
        out = r * cdf(complement(stdNormal, yy/stdev_epsilon[N+1])) - c[N];
    else
    {
        double ub= (yy - b[nn+1] + d[nn+1]) / stdev_epsilon[nn+1];      //upper limit of the integral
        ub = min(ub, (double)K);
        ub = max(ub, -(double)K);

        for (int j=0; j<=(ub+K)*STEP; j++)
            out += g(nn+1, yy-stdev_epsilon[nn+1]*((double)j/STEP-K)) * phi[j] ;

        out /= STEP;

        out += c[nn+1]-c[nn];
    }

    return out;
}

//-----------------------------------------------------------
//DP formulation of the multi-ordering model

//optimal expected profit-to-go in period t, given initial inventory level x1, updated demand mean
double V(int nn, double x1, double II);


//expected profit-to-go in period t as a function of on-hand inventory level x2 (after ordering), with updated demand mean
double G(int nn, double x2, double II)
{
    double out = 0;

    //expected profit
    if (nn==N)
    {
        //if in period N, calculate expected profit
        for (int j=0; j<=2*K*STEP; j++)
        {
            double z = (double)j/STEP-K;                        //z follows standard normal
            double d = mu + II + z*stdev_epsilon[N+1];          //d follows Normal
            out += min(x2, d)  * phi[j] ;
        }

        out *= r/STEP;
    }
    else
    {
        //if not in period N, calculate expectation with regard to signal psi_{t+1}

        //Parallelize by OpenMP Directive
        #pragma omp parallel for reduction(+:out)
        for (int j=0; j<=2*K*STEP; j++)
        {
            double epsilon_n1 = ((double)j/STEP-K) * stdev_epsilon[nn+1];
            out += V(nn+1, x2, II+epsilon_n1) * phi[j];
        }

        out /= STEP;
    }


    out -= c[nn] * x2;

    return out;
}


double V( int nn, double x1, double II)
{

    double SS = mu + II + b[nn];
	double ss = SS - d[nn];
    double out;

    if (x1 < ss)
        out = G(nn,SS,II) - k[nn] + c[nn]*x1;
    else
        out = G(nn,x1,II) + c[nn]*x1 ;

    return out;

}


//***********************************************************





int main(void)
{
    omp_set_num_threads(omp_get_num_procs());

    //Open output file
    file.open("FNV-aMMFE-FixedCost.txt", fstream::app|fstream::out);
    
    if (! file)
    {
        //if fail to open the file
        cerr << "can't open output file FNV-aMMFE-FixedCost.txt!" << endl;
        exit(EXIT_FAILURE);
    }
	
    file << setprecision(10);
    cout << setprecision(10);


    //initialize array for standard normal pdf
    for (int i=0; i<=2*K*STEP; i++)
        phi[i] = (1/sqrt(2.0*M_PI))*exp(-pow(((double)i/STEP-K),2.0)/2);


    //format output file
    cout << "r\tmu\tstdev\tT\tlambda\t";
    file << "r\tmu\tstdev\tT\tlambda\t";
	
    for (int n=1; n<=N; n++)
    {
        cout << "t" << n << "\tc" << n << "\tk" << n << "\t";
        file << "t" << n << "\tc" << n << "\tk" << n << "\t";
    }
	
    for (int n=N; n>=1; n--)
    {
        cout << "b" << n << "\t" << "d" << n << "\t";
        file << "b" << n << "\t" << "d" << n << "\t";
    }
	
    cout << "V_M\tDP_Time\tmean_M\tvar_M\tsemivar_MD\tsemivar_MU\tTotalTime" << endl;
    file << "V_M\tDP_Time\tmean_M\tvar_M\tsemivar_MD\tsemivar_MU\tTotalTime" << endl;


    //initialize parameters
    r = 2;
    mu = 1;
    c[1] = 1;

    stdev = 0.2;
    T = 0.9;
    lambda = 0.2;

    for (stdev=0.05; stdev<=0.32; stdev+=0.05)
    for (T=0.1; T<=0.91; T+=0.1)
    for (lambda=0.02; lambda<=0.21; lambda+=0.02)
    {
        double startTime = omp_get_wtime();

        //initialize time parameters
        for (int n=1; n<=N; n++)
            tau[n] = (n-1)*T/(N-1);
        tau[N+1] = 1;

        //initialize cost parameters
        for (int n=2; n<=N; n++)
            c[n] = c[n-1] + lambda;             //alternatively, c[1] + lambda*tau[n];

		//fixed costs
        for (int n=1;n<=N;n++)
            k[n] = 0.1;                         //alternatively, (double)(N-n+1)/10;

        //initialize info parameters
        var = pow(stdev, 2.0);

        for (int n=2; n<=N+1; n++)
            stdev_epsilon[n] = sqrt(tau[n]-tau[n-1])*stdev;

        //output parameters
        cout << r << "\t" << mu << "\t" << stdev << "\t" << T << "\t" << lambda << "\t";
        file << r << "\t" << mu << "\t" << stdev << "\t" << T << "\t" << lambda << "\t";
		
        for (int n=1; n<=N; n++)
        {
            cout << tau[n] << "\t" << c[n] << "\t" << k[n] << "\t";
            file << tau[n] << "\t" << c[n] << "\t" << k[n] << "\t";
        }


        //solving the multi-ordering model
        double DP_startTime = omp_get_wtime();

        //bi-sectional search for b[n] from backward
        for(int n=N; n>=1; n--)
        {
            //search for b[n]
            double lb = -20*mu;				//lower bound of y*
            double ub = 20*mu;				//upper bound of y*

            double y,temp = 1;

            while ((abs(temp)>=0.000001) && (ub-lb>0.0001))
            {
                y  = (lb+ub)/2;
                temp = g(n,y);

                if (temp<0)
                    ub = y;
                else
                    lb = y;
            }

            b[n] = y;

            //search d[n]
            double dd, intg=0;
            
            for (dd = 0; ;dd+=0.0001)
            {
                intg += g(n, b[n]-dd) ;
                
                if (intg * 0.0001 > k[n]) break;
            }
            
            d[n] = dd;

            cout << b[n] << "\t" << d[n] << "\t";
            file << b[n] << "\t" << d[n] << "\t";
        }


        //calculate expected profit
        double V_opt = V(1,0,0);

        cout << V_opt << "\t";
        file << V_opt << "\t";


    

        double DP_endTime = omp_get_wtime();

        cout << DP_endTime-DP_startTime << "\t" ;
        file << DP_endTime-DP_startTime << "\t" ;



        //======================
        //start simulation
        //======================


        // Initialize random number generator.
        knuth_b re(12345);                          //define a knuth_b random engine with seed 12345
        normal_distribution<> nd;                   //define a Normal distribution

        //calculate expected cost by simulation
        //average over RUN=10,000,000 runs
        double V_Multi_sum=0, V_Multi_sqr_sum=0;

        //for semivariance
        vector<double> profit_M(RUN);

        //Parallelize the simulation
        #pragma omp parallel for schedule(static) reduction(+:V_Multi_sum,V_Multi_sqr_sum)
        for (unsigned int j=0; j<RUN; j++)
        {
            vector<double> epsilon(N+2);            //simulated sample path of signals

            //generate signals epsilon[]
            epsilon[0] = 0;
            epsilon[1] = 0;

            for (int ii=2;ii<=N+1;ii++)
                epsilon[ii] =  stdev_epsilon[ii] * nd(re);
            


            //multi-order case
            vector<double> x(N+1), I(N+1), S(N+1), s(N+1);
            double D, v_M=0;

            x[0] = 0;		//initial inventory
            I[0] = 0;		//initial information

            //place orders and incur ordering costs
            for (int n=1;n<=N;n++)
            {
                I[n] = I[n-1] + epsilon[n];
                S[n] = mu + I[n] + b[n];
				s[n] = S[n] - d[n];

                if (x[n-1] < s[n])
                {
                    x[n] = S[n];
                    v_M -= c[n] * (x[n]-x[n-1])  + k[n];
                }
                else
                    x[n] = x[n-1];
            }

            //generate demand
            D = mu + I[N] + epsilon[N+1];
            //collect revenue
            v_M += r * min(D,x[N]);


            profit_M[j] = v_M;
            V_Multi_sum += v_M;
            V_Multi_sqr_sum += v_M*v_M;


        }


        //calculate profit mean and variance
        double mean_M = V_Multi_sum/RUN;                            //E[v]
        double mean_sqr_M = V_Multi_sqr_sum/RUN;                    //E[v^2]
        double var_M = (mean_sqr_M - mean_M*mean_M)*RUN/(RUN-1);    //sample variance


        //calculate semi-variance
        double semivar_MD=0, semivar_MU=0;
        for (int j=0;j<RUN;j++)
        {
            semivar_MD += pow(max(mean_M - profit_M[j],0.0), 2.0);
            semivar_MU += pow(max(-mean_M + profit_M[j],0.0), 2.0);
        }
        semivar_MD /= (RUN-1);
        semivar_MU /= (RUN-1);


        
        
        //output sample mean profit and confidence interval
        cout << mean_M << "\t" << var_M << "\t" << semivar_MD << "\t" << semivar_MU << "\t";
        file << mean_M << "\t" << var_M << "\t" << semivar_MD << "\t" << semivar_MU << "\t";


        double endTime = omp_get_wtime();

        cout << endTime-startTime << endl;
        file << endTime-startTime << endl;
    }


    file.close();
    return 0;
}
