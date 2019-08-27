// FEMuscleMaterial.cpp: implementation of the FEMuscleMaterial class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "FEMuscleMaterial.h"

#ifndef SQR
	#define SQR(x) ((x)*(x))
#endif

// define the material parameters
BEGIN_PARAMETER_LIST(FEMuscleMaterial, FETransverselyIsotropic)
	ADD_PARAMETER(m_G1, FE_PARAM_DOUBLE, "g1");
	ADD_PARAMETER(m_G2, FE_PARAM_DOUBLE, "g2");
	ADD_PARAMETER(m_G3, FE_PARAM_DOUBLE, "g3");
	ADD_PARAMETER(m_P1, FE_PARAM_DOUBLE, "p1");
	ADD_PARAMETER(m_P2, FE_PARAM_DOUBLE, "p2");
	ADD_PARAMETER(m_Lofl, FE_PARAM_DOUBLE, "Lofl");
	ADD_PARAMETER(m_smax, FE_PARAM_DOUBLE, "smax");
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
#ifdef WIN32
///here warning C28251: Inconsistent annotation for function: this instance has an error
//inline double acosh(double x)
inline double acoshMuscle(double x)
{
	if (x <= 1) return 0;
	return log(x + sqrt(x*x - 1));
}
#endif

/////////////////////////////////////////////////////////////////////////
// FEMuscleMaterial
/////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
//! Calculates the deviatoric stress at a material point.
//!
mat3ds FEMuscleMaterial::DevStress(FEMaterialPoint& mp)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();

	// deformation gradient
	mat3d &F = pt.m_F;
	double J = pt.m_J;

	// get the initial fiber direction
	vec3d a0;
	a0.x = pt.m_Q[0][0];
	a0.y = pt.m_Q[1][0];
	a0.z = pt.m_Q[2][0];

	// calculate the current material axis lam*a = F*a0;
	vec3d a = F*a0;

	// normalize material axis and store fiber stretch
	double la = a.unit();
	double lat = la*pow(J, -1.0/3.0); // i.e. lambda tilde = sqrt(I4)

	// calculate deviatoric left Cauchy-Green tensor: B = Jm23*F*Ft
	mat3ds B = pt.DevLeftCauchyGreen();

	// calculate square of B
	mat3ds B2 = B*B;

	// calculate Ba = B*a
	vec3d Ba = B*a;

	// ----- invariants of B -----
	double I1 = B.tr();
	double I2 = 0.5*(I1*I1 - B2.tr() );
	double I4 = lat*lat;
	double I5 = I4*(a*Ba);

	// ----- calculate new invariants b1 and b2 of B ------

	// note that we need to be carefull about roundoff errors
	// since these functions may create numerical problems

	double g = I5/(I4*I4) - 1;
	double b1 = (g > 0 ? sqrt(g) : 0);

	///here warning C28251: Inconsistent annotation for function: this instance has an error 
	double b2 = acoshMuscle(0.5*(I1*I4 - I5) / lat);

	// calculate omage (w)
	double I4r = sqrt(I4);
	double w = 0.5*(I1*I4 - I5)/sqrt(I4);

	// set beta and ksi to their limit values
	double beta = 1.0;
	double ksi = -1.0/3.0;

	// if w not equals unity, we can calculate beta and ksi
	if (w > 1.0001)
	{
		beta = b2/sqrt(w*w-1);
		ksi = (1.0/(w*w-1))*(1 - w*b2/sqrt(w*w-1));
	}

	// ----- calculate strain derivatives -----

	// We assume that W(I1, I4, I5, alpha) = F1(B1(I4, I5)) + F2(B2(I1,I4,I5)) + F3(lam(I4), alpha)
	double W1, W2, W4, W5;

	// calculate derivatives for F1
	double F1D4 = -2*m_G1*(I5/(I4*I4*I4));
	double F1D5 = m_G1/(I4*I4);

	// calculate derivatives for F2
	double F2D1 =  m_G2*beta*lat;
	double F2D4 =  m_G2*beta*(I1*I4 + I5)*0.5*pow(I4, -1.5);
	double F2D5 = -m_G2*beta/lat;

	// calculate derivatives for F3
	// these terms are proposed to fix the zero-stress problem
	double F3D4 = 9.0*m_G3*0.125*log(I4)/I4;

	// calculate passive fiber force
	double Fp;
/*	if (lat <= m_Lofl)
	{
		Fp = 0;
	}
	else if (lat < lam1)
*/	if (lat < m_fib.m_lam1)
	{
		Fp = m_P1*(exp(m_P2*(lat/m_Lofl - 1)) - 1);
	}
	else
	{
		double P3, P4;

		P3 = m_P1*m_P2*exp(m_P2*(m_fib.m_lam1/m_Lofl - 1));
		P4 = m_P1*(exp(m_P2*(m_fib.m_lam1/m_Lofl - 1)) - 1) - P3*m_fib.m_lam1/m_Lofl;

		Fp = P3*lat/m_Lofl + P4;
	}

	// calculate active fiber force
	double Fa=0;

	if ((lat <= 0.4*m_Lofl) || (lat >= 1.6*m_Lofl))
	{
		// we have added this part to make sure that 
		// Fa is zero outside the range [0.4, 1.6] *m_Lofl
		Fa = 0;
	}
	else
	{
		if (lat <= 0.6*m_Lofl)
		{
			Fa = 9*SQR(lat/m_Lofl - 0.4);
		}
		else if (lat >= 1.4*m_Lofl)
		{
			Fa = 9*SQR(lat/m_Lofl - 1.6);
		}
		else if ((lat >= 0.6*m_Lofl) && (lat <= 1.4*m_Lofl))
		{
			Fa = 1 - 4*SQR(1 - lat/m_Lofl);
		}
	}

	// activation level
	double alpha = m_fib.GetActivation();

	// calculate total fiber force
	double FfDl = m_smax*(Fp + alpha*Fa)/m_Lofl;
	double FfD4  = 0.5*FfDl/lat;

	// add all derivatives
	W1 = F2D1;
	W2 = 0;
	W4 = F1D4 + F2D4 + F3D4 + FfD4;
	W5 = F1D5 + F2D5;

	// dyadic of a
	mat3ds AxA = dyad(a);

	// symmetric dyad of a and Ba
	mat3ds ABA = dyads(a, Ba);

	// ----- calculate stress -----

	// calculate T 
	mat3ds T = B*(W1 + W2*I1) - B2*W2 + AxA*(I4*W4) + ABA*(I4*W5);

	return T.dev()*(2.0/J);
}

//-----------------------------------------------------------------------------
//! Calculates the spatial deviatoric tangent at a material point
//!
tens4ds FEMuscleMaterial::DevTangent(FEMaterialPoint& mp)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();

	// deformation gradient
	mat3d &F = pt.m_F;
	double J = pt.m_J;

	// deviatoric cauchy-stress, trs = trace[s]/3
	mat3ds devs = pt.m_s.dev();

	// get the initial fiber direction
	vec3d a0;
	a0.x = pt.m_Q[0][0];
	a0.y = pt.m_Q[1][0];
	a0.z = pt.m_Q[2][0];

	// calculate the current material axis lam*a = F*a0;
	vec3d a = F*a0;

	// normalize material axis and store fiber stretch
	double la  = a.unit();
	double lat = la*pow(J, -1.0/3.0); // i.e. lambda tilde

	// get deviatoric left Cauchy-Green tensor
	mat3ds B = pt.DevLeftCauchyGreen();

	// calculate square of B
	mat3ds B2 = B*B;

	// calculate B*a
	vec3d Ba = B*a;

	// invariants of B
	double I1, I2, I4, I5;

	I1 = B.tr();
	I2 = 0.5*(I1*I1 - B2.tr());
	I4 = lat*lat;
	I5 = I4*(a*Ba);

	// calculate new invariants
	double g = I5/(I4*I4) - 1;
	double b1 = (g > 0 ? sqrt(g) : 0);

	///here warning C28251: Inconsistent annotation for function: this instance has an error
	double b2 = acoshMuscle(0.5*(I1*I4 - I5) / lat);

	// calculate omage (w)
	double w = 0.5*(I1*I4 - I5)/lat;

	// set beta and ksi to their limit values
	double beta = 1.0;
	double ksi = -1.0/3.0;

	// if w not equals unity, we can calculate beta and ksi
	if (w > 1.0001)
	{
		beta = b2/sqrt(w*w-1);
		ksi = (1.0/(w*w-1))*(1 - w*b2/sqrt(w*w-1));
	}

	// --- strain energy derivatives ---
	// We assume that W(I1, I4, I5, alpha) = F1(B1(I4, I5)) + F2(B2(I1,I4,I5)) + F3(lam(I4), alpha)
	double W1, W2, W4, W5;

	// -- A. matrix contribution --
	// calculate derivatives for F1
	double F1D4 = -2*m_G1*(I5/(I4*I4*I4));
	double F1D5 = m_G1/(I4*I4);

	double F1D44 = 6*m_G1*(I5/(I4*I4*I4*I4));
	double F1D45 = -2*m_G1/(I4*I4*I4);

	// calculate derivatives for F2
	double F2D1 =  m_G2*beta*lat;
	double F2D4 =  m_G2*beta*(I1*I4 + I5)*0.5*pow(I4, -1.5);
	double F2D5 = -m_G2*beta/lat;

	double F2D11 = ksi*m_G2*I4*0.5;
	double F2D44 = 2.0*m_G2*ksi*pow(0.25*(I1*I4+I5)/pow(I4, 1.5), 2) - m_G2*beta*(0.25*(I1*I4 + 3*I5) / pow(I4, 2.5));
	double F2D55 = 0.5*m_G2*ksi/I4;
	double F2D14 = m_G2*beta*0.5/lat + m_G2*ksi*(I1*I4+I5)*0.25/I4;
	double F2D15 = -0.5*m_G2*ksi;
	double F2D45 = m_G2*beta*0.5*pow(I4, -1.5) - m_G2*ksi*0.25*(I1*I4+I5)/(I4*I4);

	// calculate derivatives for F3
	// these terms are proposed to fix the zero-stress problem
	double F3D4  = 9.0*m_G3*0.125*log(I4)/I4;
	double F3D44 = 9.0*m_G3*0.125*(1 - log(I4))/(I4*I4);

	// -- B. fiber contribution --

	// calculate passive fiber force
	double Fp, FpDl;
/*	if (lat <= m_Lofl)
	{
		Fp = 0;
		FpDl = 0;
	}
	else if (lat < lam1)
*/	if (lat < m_fib.m_lam1)
	{
		Fp = m_P1*(exp(m_P2*(lat/m_Lofl - 1)) - 1);
		FpDl = m_P1*m_P2*exp(m_P2*(lat/m_Lofl-1))/m_Lofl;
	}
	else
	{
		double P3, P4;

		P3 = m_P1*m_P2*exp(m_P2*(m_fib.m_lam1/m_Lofl - 1));
		P4 = m_P1*(exp(m_P2*(m_fib.m_lam1/m_Lofl - 1)) - 1) - P3*m_fib.m_lam1/m_Lofl;

		Fp = P3*lat/m_Lofl + P4;
		FpDl = P3/m_Lofl;
	}

	// calculate active fiber force
	double Fa=0, FaDl=0;

	if ((lat <= 0.4*m_Lofl) || (lat >= 1.6*m_Lofl))
	{
		// we have added this part to make sure that 
		// Fa is zero outside the range [0.4, 1.6] *m_Lofl
		Fa = 0;
		FaDl = 0;
	}
	else
	{
		if (lat <= 0.6*m_Lofl)
		{
			Fa = 9*SQR(lat/m_Lofl - 0.4);
			FaDl = 18*(lat/m_Lofl - 0.4)/m_Lofl;
		}
		else if (lat >= 1.4*m_Lofl)
		{
			Fa = 9*SQR(lat/m_Lofl - 1.6);
			FaDl = 18*(lat/m_Lofl - 1.6)/m_Lofl;
		}
		else if ((lat >= 0.6*m_Lofl) && (lat <= 1.4*m_Lofl))
		{
			Fa = 1 - 4*SQR(1 - lat/m_Lofl);
			FaDl = 8*(1 - lat/m_Lofl)/m_Lofl;
		}
	}

	// activation level
	double alpha = m_fib.GetActivation();

	// calculate total fiber force
	double FfDl = m_smax*(Fp + alpha*Fa)/m_Lofl;
	double FfD4  = 0.5*FfDl/lat;

	double FfDll = m_smax*(FpDl + alpha*FaDl)/m_Lofl;
	double FfD44 = 0.25*(FfDll - FfDl / lat)/I4;

	// add all derivatives
	W1 = F2D1;
	W2 = 0;
	W4 = F1D4 + F2D4 + F3D4 + FfD4;
	W5 = F1D5 + F2D5;

	// calculate second derivatives
	double W11, W12, W22, W14, W24, W15, W25, W44, W45, W55;

	W11 = F2D11;
	W12 = 0;
	W22 = 0;
	W14 = F2D14;
	W24 = 0;
	W15 = F2D15;
	W25 = 0;
	W44 = F1D44 + F2D44 + F3D44 + FfD44;
	W45 = F1D45 + F2D45;
	W55 = F2D55;

	// calculate dWdC:C
	double WCC = W1*I1 + 2*W2*I2 + W4*I4 + 2*W5*I5;

	// calculate C:d2WdCdC:C
	double CW2CCC = (W11*I1 + W12*I1*I1 + W2*I1 + 2*W12*I2 + 2*W22*I1*I2 + W14*I4 + W24*I1*I4 + 2*W15*I5 + 2*W25*I1*I5)*I1
		           -(W12*I1 + 2*W22*I2 + W2 + W24*I4 + 2*W25*I5)*(I1*I1 - 2*I2)
				   +(W14*I1 + 2*W24*I2 + W44*I4 + 2*W45*I5)*I4 + (W15*I1 + 2*W25*I2 + W45*I4 + 2*W55*I5)*2*I5
				   + 2*W5*I5;

	// second order identity tensor
	mat3dd ID(1);

	// 4th order identity tensors
	tens4ds IxI = dyad1s(ID);
	tens4ds I   = dyad4s(ID);

	// dyad of a
	mat3ds AxA = dyad(a);

	// --- calculate elasticity tensor ---

	// calculate push-forward of dI5/dC = I4*(a*Ba + Ba*a)
	mat3ds I5C = dyads(a, Ba)*I4;

	// calculate push-forward of d2I5/dCdC
	tens4ds I5CC = dyad4s(AxA, B)*I4;

	// calculate push forward of I
	tens4ds Ib = dyad4s(B);

	// calculate push forward of dW/dCdC:C
	mat3ds WCCC = 
		B*(W11*I1 + W12*I1*I1 + W2*I1 + 2*W12*I2 + 2*W22*I1*I2 + W14*I4 + W24*I1*I4 + 2*W15*I5 + 2*W25*I1*I5) - 
		B2*(W12*I1 + 2*W22*I2 + W2 + W24*I4 + 2*W25*I5) + 
		AxA *((W14*I1 + 2*W24*I2 + W44*I4 + 2*W45*I5)*I4) + 
		I5C*(W15*I1 + 2*W25*I2 + W45*I4 + 2*W55*I5 + W5);

	// calculate push-forward of dW2/dCdC
	tens4ds W2CC = 
		dyad1s(B)*(W11 + 2.0*W12*I1 + W2 + W22*I1*I1) + 
		dyad1s(B, B2)*(-(W12+W22*I1)) + dyad1s(B2)*W22 - Ib*W2 +
		dyad1s(B, AxA)*((W14 + W24*I1)*I4) +
		dyad1s(B, I5C)*(W15 + W25*I1) + 
		dyad1s(B2, AxA)*(-W24*I4) + 
		dyad1s(AxA)*(W44*I4*I4) + 
		dyad1s(AxA, I5C)*(W45*I4) + 
		dyad1s(I5C)*W55 + I5CC*W5;

	// let's put it all together
	// cw
	tens4ds cw =  IxI*((4.0/(9.0*J))*(CW2CCC)) + W2CC*(4/J) - dyad1s(WCCC, ID)*(4.0/(3.0*J));

	// elasticity tensor
	tens4ds c = dyad1s(devs, ID)*(-2.0/3.0) + (I - IxI/3.0)*(4.0*WCC/(3.0*J)) + cw;

	return tens4ds(c);
}
