//=========================================================
// ShuttleD 1.0 source code
// This is what I have been working on for the last few months. The Shuttle-D is currently designed as a NTR propelled cargo transport
// & utility vessel compatible with UMMU, UCGO, & Orbitersound. Ive made sure to heavily comment my source in order to give new addon devs
// a good resource for understanding many of the pitfalls I had to endure, and how to beat them. If youre planning on reading this as a tutorial,
// I would reccomend you start over in D9base.h, as I structured the tutorial starting from there.
//=========================================================

#define STRICT
#define ORBITER_MODULE
#include "D9base.h"
#include "UCGOCargoSDK.h"
#include "orbitersdk.h"
#include <math.h>
#include <stdio.h>
#include "OrbiterSoundSDK40.h"
#include "VesselAPI.h"

// these are definitions for sound so functions will be a bit clearer to read..		
#define GEARUP						 1
#define GEARDOWN					 2
#define PLBAYAOPEN					 3
#define PLBAYACLOSE					 4
#define PLBAYBOPEN					 5
#define PLBAYBCLOSE					 6

HINSTANCE g_hDLL;
VISHANDLE MainExternalMeshVisual = 0;

// ==============================================================
// Airfoil/Aerodynamics definition
// Looks painfuly complicated at first, but gets a lot simpler once you realize that each column corresponds to a particular AOA value,
// and the parameters underneath give the aerodynamic behaviour of the vessel at the given AOA. An excellent tutorial on what these values mean
// is available on the Orbiterwiki website.
// ==============================================================

void Shuttle_MomentCoeff (double aoa,double M,double Re,double *cl,double *cm,double *cd)
{
	int i;
	const int nabsc = 7;
	static const double AOA[nabsc] = {-180*RAD, -90*RAD,-30*RAD, 0*RAD, 60*RAD,90*RAD,180*RAD};
	static const double CL[nabsc]  = {       0,     -0.0005,   -0.001,     0,     0.0071,     0.0011,      0.00001};
	static const double CM[nabsc]  = {       0,      0,   0.0007,  0,-0.0010,     0,      0};

	for (i = 0; i < nabsc-1 && AOA[i+1] < aoa; i++);
	double f = (aoa-AOA[i]) / (AOA[i+1]-AOA[i]);
	*cl = CL[i] + (CL[i+1]-CL[i]) * f;  // aoa-dependent lift coefficient
	*cm = CM[i] + (CM[i+1]-CM[i]) * f;  // aoa-dependent moment coefficient
	double saoa = sin(aoa);
	double pd = 0.045 + 0.4*saoa*saoa;  // profile drag
	*cd = pd + oapiGetInducedDrag (*cl, 0.1,0.7) + oapiGetWaveDrag (M, 0.75, 1.0, 1.1, 0.04);
	// profile drag + (lift-)induced drag + transonic/supersonic wave (compressibility) drag
}

// ==============================================================
// ShuttleD::ShuttleD (OBJHANDLE hObj, int fmodel)
// To the best of my knowledge, this is called when a Shuttle-D is first created & allows parameters to be set to specific values right away.
// For example 	O2Tank = 1000; indicates that the main oxygen tank parameter is set to 1000/1000; full, upon creation of a new Shuttle-D.
// ==============================================================

ShuttleD::ShuttleD (OBJHANDLE hObj, int fmodel)
	: VESSEL3 (hObj, fmodel)
{
	GEAR_status = GEAR_UP;
	GEAR_proc = 0.0;
	PLBAYA_status = PLBAYA_UP;
	PLBAYA_proc = 0.0;
	PLBAYB_status = PLBAYB_UP;
	PLBAYB_proc = 0.0;
	O2Tank = 1000;

	DefineAnimations();
}

//=========================================================
// Vessel Animations
// This part of the code refers to the nuts & bolts involved in givig this craft animated parts, in this case retractable/extendable gear,
// as well as two payload bay doors which can be opened or closed. 
//=========================================================
void ShuttleD::DefineAnimations()
{
	//ANIMATIONCOMPONENT_HANDLE parent;
	static UINT gearback[1] = {38}; // {38} refers to the mesh group used in the animation
	static MGROUP_ROTATE gear (
		0,
		gearback,
		1, // "1" here points to the mesh index the animation is performed on (VC & external meshes are loaded seperately in this case, so the external is #1 
		_V(0,-2.0,-27.97), // the pivot point around which the rotational animation occurs in 3d coordinates.
		_V(1,0,0), // the axis of rotation arund which the animation occurs. Picture it as the bolt used to mount the hinge, except in this case its just the directonal vector for it. I could jut as easily put in (-1,0,0) fir this except that would reverse the rotational angle & force me to change that...
		(float)(-90*RAD)// The amount of "turn" in degrees that the animation performs each time it happens. In this case, the gear have to retract from vertical to horizontal so it 90 degrees;	(-90*RAD)
		);
	static UINT gearfront[1] = {39};
	static MGROUP_ROTATE gearf (
		0,
		gearfront,
		1,
		_V(0,-2.0,24.73),
		_V(1,0,0),
		(float)(90*RAD)
		);
	anim_gear = CreateAnimation (0);
	AddAnimationComponent (anim_gear, 0, 1, &gear);
	AddAnimationComponent (anim_gear, 0, 1, &gearf);

	static UINT PLBAYA[1] = {33};
	static MGROUP_ROTATE PLBAYA1 (
		0,
		PLBAYA,
		1,
		_V(0.858,-1.821666,-5.07),
		_V(0,0,1),
		(float)(90*RAD)
		);
	anim_PLBAYA = CreateAnimation (0);
	AddAnimationComponent (anim_PLBAYA, 0, 1, &PLBAYA1);

	static UINT PLBAYB[1] = {29};
	static MGROUP_ROTATE PLBAYB1 (
		0,
		PLBAYB,
		1,
		_V(-1.08,-3.587,-21.594),
		_V(0,0,1),
		(float)(-90*RAD)
		);
	anim_PLBAYB = CreateAnimation (0);
	AddAnimationComponent (anim_PLBAYB, 0, 1, &PLBAYB1);
}

//=========================================================
// Revert Gear Function
// This function is designed so that anytime it is called the landing gear will raise, lower, or reverse their previous motion
// Ive broken up mine into 2 further subdivied functions Geardown & Gearup so that my VC buttons have a more logical layout to them than simply
// "do what youre not doing right now"
//=========================================================

void ShuttleD::RevertGEAR (void)
{
	GEAR_status = ((GEAR_status == GEAR_UP || GEAR_status == GEAR_RAISING) ?
GEAR_LOWERING : GEAR_RAISING);
}

double ShuttleD::Geardown ()
{
if (GEAR_proc == 1.000)
		{
RevertGEAR();
		strcpy(SendHudMessage(),"Gear lowering");
	}
else
		{
	strcpy(SendHudMessage(),"Gear down or in motion");
	}
return true;
}

double ShuttleD::Gearup ()
{
if (GEAR_proc == 0.000)
		{
RevertGEAR();
	strcpy(SendHudMessage(),"Gear raising");
	}
else
		{
	strcpy(SendHudMessage(),"Gear up or in motion");
	}
return true;
}

//=========================================================
// Revert Main Payload Bay Doors Function
// This function is designed so that anytime it is called the Main bay doors will raise, lower, or reverse their previous motion
// Ive broken up mine into 2 further subdivied functions MainBayOpen & MainBayClose so that my VC buttons have a more logical layout to them than simply
// "do what youre not doing right now"
//=========================================================

void ShuttleD::RevertPLBAYA (void)
{
	PLBAYA_status = ((PLBAYA_status == PLBAYA_UP || PLBAYA_status == PLBAYA_CLOSING) ?
PLBAYA_OPENING : PLBAYA_CLOSING);
}

double ShuttleD::MainBayOpen ()
{
if (PLBAYA_proc == 0.000)
		{
RevertPLBAYA();
		strcpy(SendHudMessage(),"Main Payload bay opening");
	}
else
		{
	strcpy(SendHudMessage(),"Main bay open or in motion");
	}
return true;
}

double ShuttleD::MainBayClose ()
{
if (PLBAYA_proc == 1.000)
		{
RevertPLBAYA();
	strcpy(SendHudMessage(),"Main Payload Bay Closing");
	}
else
		{
	strcpy(SendHudMessage(),"Main bay closed or in motion");
	}
return true;
}

//=========================================================
// Revert Auxiliary Payload Bay Doors Function
// This function is designed so that anytime it is called the Aux bay doors will raise, lower, or reverse their previous motion
// Ive broken up mine into 2 further subdivied functions AuxBayOpen & AuxBayClose so that my VC buttons have a more logical layout to them than simply
// "do what youre not doing right now"
//=========================================================

void ShuttleD::RevertPLBAYB (void)
{
	PLBAYB_status = ((PLBAYB_status == PLBAYB_UP || PLBAYB_status == PLBAYB_CLOSING) ?
PLBAYB_OPENING : PLBAYB_CLOSING);
}

double ShuttleD::AuxBayOpen ()
{
if (PLBAYB_proc == 0.000)
		{
RevertPLBAYB();
		strcpy(SendHudMessage(),"Main Payload bay opening");
	}
else
		{
	strcpy(SendHudMessage(),"Main bay open or in motion");
	}
return true;
}

double ShuttleD::AuxBayClose ()
{
if (PLBAYB_proc == 1.000)
		{
RevertPLBAYB();
	strcpy(SendHudMessage(),"Main Payload Bay Closing");
	}
else
		{
	strcpy(SendHudMessage(),"Main bay closed or in motion");
	}
return true;
}

//=========================================================
// Mass Update Function
// A big prize to anyone who can guess what this does! Yes that is correct, it updates the mass of a Shuttle-D vessel based on its empty mass & the amount of O2 it is carrying!
// As a previous English teacher I once had was fond of saying, your prize is knoledge & undertanding (shows how much I learned :)
// This function does not fully complete the mass calculations, as the UCGO mass update function down in void clbkPostStep also has a hand in it, but this is the main place or things of that ilk, so...
//=========================================================

double ShuttleD::UpdateMass ()
{
double mass=(EXP_EMPTYMASS);
double massand=(O2Tank);

mass+=O2Tank;

return mass;
}

//=========================================================
// O2Check Function
// A very simple function here, that allows me to return the value of oxygen remaining in the tanks.
// I needed this in order for the hud check function that returns O2 levels to the pilot would work.
//=========================================================
double ShuttleD::O2Check ()
{
return O2Tank;
}

//=========================================================
// Vessel Capabilities
// This is what you might consider the core of the addon. In clbkSetClassCaps, all of the core information about the way a vessel behaves
// as a physics object is declared. This includes empty mass, PMI (a measure of the dynamics involved in rotating the vessel), cross-sections,
// Albedo (the colour of the white dot when you zoom out), gear friction coefficients, thrusters & engines, the size of the vessel,
// & of course its mesh (what it looks like). Also included here in this addon are the UMMU & UCGO calls which specify how the vessel uses
// those libraries. They are fairly self-explanatory, so I wont go into detail on them here.
//=========================================================
void ShuttleD::clbkSetClassCaps (FILEHANDLE cfg)
{
	Crew.InitUmmu(GetHandle());	
	Crew.DefineAirLockShape(TRUE,-1,1,-5.04,2.84, 23.93,26.93);
	Crew.SetMembersPosRotOnEVA(_V(0,-0.937,25.935),_V(0,-225,0));
	float UMmuVersion=Crew.GetUserUMmuVersion();
	//	double UMmuVersion=Crew.GetUserUMmuVersion();
	Crew.SetMaxSeatAvailableInShip(2);

	Crew.DeclareActionArea(0,_V(2,-3.05,12.63),2.9,TRUE,"Sound\\ShuttleD\\ActionCommand.wav","Payload Bay A Activated");
	Crew.DeclareActionArea(1,_V(0,3.120,-22.568),2.9,TRUE, "Sound\\ShuttleD\\ActionCommand.wav","Payload Bay B Activated");
	Crew.DeclareActionArea(2,_V(0,-0.937,26.2),4.5,TRUE,"Sound\\ShuttleD\\ActionCommand.wav","Airlock Activated");



	iActionAreaDemoStep=0;	// this is just to show a feature of action area, see below "DetectActionAreaActivated"

	SelectedUmmuMember =0;  // our current selected member


	// The HUD display method variables, see PDF doc
	cUmmuHudDisplay[0] =0;	// Initialisation of UMmu hud char variable
	dHudMessageDelay =0;	// Initialisation of UMmu delay variable
	strcpy(SendHudMessage(),"Welcome aboard. Press E to EVA, 1/2 to select crew, A to Open/Close airlock, 0 or 8 for info, and M to add crew.");

	// The Add mmu without scenery editor variable see PDF doc
	cAddUMmuToVessel[0]=0;

	//UCGO 2.0 Initialisation, cargo slot pos, rot declaration
	hUcgo.Init(GetHandle());
	// in contrary of the PDF code we declare 6 slots
	// because it's fun :)
	hUcgo.DeclareCargoSlot(0,_V(-0.65,-3.3,11.53),_V(0,0,270)); // slot 0
	hUcgo.DeclareCargoSlot(1,_V(-0.65,-3.3,10.23),_V(0,0,270)); // slot 1
	hUcgo.DeclareCargoSlot(2,_V(-0.65,-3.3,8.93),_V(0,0,270)); // slot 2
	hUcgo.DeclareCargoSlot(3,_V(-0.65,-3.3,7.63),_V(0,0,270)); // slot 3
	hUcgo.DeclareCargoSlot(4,_V(-0.65,-3.3,6.33),_V(0,0,270)); // slot 4
	hUcgo.DeclareCargoSlot(5,_V(-0.65,-3.3,5.03),_V(0,0,270)); // slot 5
	hUcgo.DeclareCargoSlot(6,_V(-0.65,-3.3,3.73),_V(0,0,270)); // slot 6
	hUcgo.DeclareCargoSlot(7,_V(-0.65,-3.3,2.43),_V(0,0,270)); // slot 7
	hUcgo.DeclareCargoSlot(8,_V(-0.65,-3.3,1.13),_V(0,0,270)); // slot 8
	hUcgo.DeclareCargoSlot(9,_V(-0.65,-3.3,-0.17),_V(0,0,270)); // slot 9
	hUcgo.DeclareCargoSlot(10,_V(-0.65,-3.3,-1.47),_V(0,0,270)); // slot 10
	hUcgo.DeclareCargoSlot(11,_V(-0.65,-3.3,-2.77),_V(0,0,270)); // slot 11
	hUcgo.DeclareCargoSlot(12,_V(-0.65,-3.3,-4.07),_V(0,0,270)); // slot 12
	hUcgo.DeclareCargoSlot(13,_V(-0.65,-3.3,-5.37),_V(0,0,270)); // slot 13
	hUcgo.DeclareCargoSlot(14,_V(-0.65,-3.3,-6.67),_V(0,0,270)); // slot 14
	hUcgo.DeclareCargoSlot(15,_V(-0.65,-3.3,-7.97),_V(0,0,270)); // slot 15
	hUcgo.DeclareCargoSlot(16,_V(-0.65,-3.3,-9.27),_V(0,0,270)); // slot 16
	hUcgo.DeclareCargoSlot(17,_V(-0.65,-3.3,-10.57),_V(0,0,270)); // slot 17

	hUcgo.SetSlotGroundReleasePos(0,_V(2,-3.6,11.53)); // slot 0
	hUcgo.SetSlotGroundReleasePos(1,_V(2,-3.6,10.23)); // slot 1
	hUcgo.SetSlotGroundReleasePos(2,_V(2,-3.6,8.93)); // slot 2
	hUcgo.SetSlotGroundReleasePos(3,_V(2,-3.6,7.63)); // slot 3
	hUcgo.SetSlotGroundReleasePos(4,_V(2,-3.6,6.33)); // slot 4
	hUcgo.SetSlotGroundReleasePos(5,_V(2,-3.6,5.03)); // slot 5
	hUcgo.SetSlotGroundReleasePos(6,_V(2,-3.6,3.73)); // slot 6
	hUcgo.SetSlotGroundReleasePos(7,_V(2,-3.6,2.43)); // slot 7
	hUcgo.SetSlotGroundReleasePos(8,_V(2,-3.6,1.13)); // slot 8
	hUcgo.SetSlotGroundReleasePos(9,_V(2,-3.6,-0.17)); // slot 9
	hUcgo.SetSlotGroundReleasePos(10,_V(2,-3.6,-1.47)); // slot 10
	hUcgo.SetSlotGroundReleasePos(11,_V(2,-3.6,-2.77)); // slot 11
	hUcgo.SetSlotGroundReleasePos(12,_V(2,-3.6,-4.07)); // slot 12
	hUcgo.SetSlotGroundReleasePos(13,_V(2,-3.6,-5.37)); // slot 13
	hUcgo.SetSlotGroundReleasePos(14,_V(2,-3.6,-6.67)); // slot 14
	hUcgo.SetSlotGroundReleasePos(15,_V(2,-3.6,-7.97)); // slot 15
	hUcgo.SetSlotGroundReleasePos(16,_V(2,-3.6,-9.27)); // slot 16
	hUcgo.SetSlotGroundReleasePos(17,_V(2,-3.6,-10.57)); // slot 17

	// UCGO 2.0 Parameters settings
	hUcgo.SetReleaseSpeedInSpace(0.001f);	   // release speed of cargo in space in m/s
	hUcgo.SetMaxCargoMassAcceptable(50000.0);   // max cargo mass in kg that your vessel can carry
	hUcgo.SetGrappleDistance(60);		       // grapple distance radius in meter from center of ship


	// UCGO Variables initialisation
	cCargoHudDisplay[0]=0;						// Cargo hud display char variable
	dCargHudMessageDelay=0;						// Cargo hud display delay
	iSelectedCargo=-1;							// for the selection of cargos (-1 mean "default" see header)
	// welcome message with keys for users
	strcpy(SendCargHudMessage(),"Payload Controls C/Shift+C to grapple/release, 9/Shift+9 to add cargo.");

	THRUSTER_HANDLE th_main, th_rcs[14], th_group[4];
	DOCKHANDLE Dock0;


	// ************************ Airfoil  ****************************
	ClearAirfoilDefinitions();
	CreateAirfoil (LIFT_VERTICAL, _V(0,0,0), Shuttle_MomentCoeff,  8, 140, 0.1);


	// vessel caps definitions

	AddMesh ("Shuttle-D");
	SetMeshVisibilityMode (AddMesh (DVCInterior = oapiLoadMeshGlobal ("ShuttleDVC")), MESHVIS_VC);
	

	SetCameraOffset (_V(0,0.14,24.13));
	SetAlbedoRGB (_V(0.77,0.20,0.73));
	SetSize (EXP_SIZE);



	SetPMI (EXP_PMI);
	SetCrossSections (EXP_CS);
	SetSurfaceFrictionCoeff (0.55, 0.79);
	SetRotDrag (_V(0.9, 0.76, 0.2));

	EnableTransponder (true);
	InitNavRadios (4);


	// propellant resources
	PROPELLANT_HANDLE MainFuel = CreatePropellantResource (EXP_FUELMASS);
	PROPELLANT_HANDLE RCS1 = CreatePropellantResource (EXP_RCS1FUELMASS);
	PROPELLANT_HANDLE RCS2 = CreatePropellantResource (EXP_RCS2FUELMASS);
	PROPELLANT_HANDLE RCS3 = CreatePropellantResource (EXP_RCS3FUELMASS);
	PROPELLANT_HANDLE RCS4 = CreatePropellantResource (EXP_RCS4FUELMASS);
	PROPELLANT_HANDLE RCS5 = CreatePropellantResource (EXP_RCS5FUELMASS);
	PROPELLANT_HANDLE RCS6 = CreatePropellantResource (EXP_RCS6FUELMASS);
	PROPELLANT_HANDLE RCS7 = CreatePropellantResource (EXP_RCS7FUELMASS);
	PROPELLANT_HANDLE RCS8 = CreatePropellantResource (EXP_RCS8FUELMASS);
	PROPELLANT_HANDLE RCS9 = CreatePropellantResource (EXP_RCS9FUELMASS);
	PROPELLANT_HANDLE RCS10 = CreatePropellantResource (EXP_RCS10FUELMASS);
	PROPELLANT_HANDLE RCS11 = CreatePropellantResource (EXP_RCS11FUELMASS);
	PROPELLANT_HANDLE RCS12 = CreatePropellantResource (EXP_RCS12FUELMASS);

	// main engine
	th_main = CreateThruster (_V(0,0,-35.82), _V(0,0,1), EXP_MAXMAINTH, MainFuel, VACSHD_ISP, NMLSHD_ISP, P_NML);
	CreateThrusterGroup (&th_main, 1, THGROUP_MAIN);
	SURFHANDLE texmain = oapiRegisterExhaustTexture ("ShuttleDMainExhaust");
	AddExhaust (th_main, 2.99, 1.80, _V(0,0,-35.82), _V(0,0,-4.1), texmain);

	PARTICLESTREAMSPEC contrail_main = {
		0, 5.0, 16, 200, 0.15, 1.0, 5, 3.0, PARTICLESTREAMSPEC::DIFFUSE,
		PARTICLESTREAMSPEC::LVL_PSQRT, 0, 2,
		PARTICLESTREAMSPEC::ATM_PLOG, 1e-4, 1
	};
	PARTICLESTREAMSPEC exhaust_main = {
		0, 2.0, 20, 200, 0.05, 0.1, 8, 1.0, PARTICLESTREAMSPEC::EMISSIVE,
		PARTICLESTREAMSPEC::LVL_SQRT, 0, 1,
		PARTICLESTREAMSPEC::ATM_PLOG, 1e-5, 0.1
	};
	AddExhaustStream (th_main, _V(0,0.13,-37.52), &contrail_main);
	AddExhaustStream (th_main, _V(0,0.13,-37.52), &exhaust_main);

	// RCS engines
	th_rcs[0] = CreateThruster (_V( 1.611,0, 24.707), _V(0,0,1), RCSTH0, RCS1,  VACRCS_ISP, NMLRCS_ISP, P_NML);//CM FRONT RIGHT SIDE FORWARD
	th_rcs[1] = CreateThruster (_V( -1.611,0, 24.707), _V(0,0,1), RCSTH1, RCS2,  VACRCS_ISP, NMLRCS_ISP, P_NML);//CM FRONT LEFT SIDE FORWARD

	th_rcs[2] = CreateThruster (_V(1.8505,0, 24.306), _V(-1, 0,0), RCSTH2, RCS1,  VACRCS_ISP, NMLRCS_ISP, P_NML);//CM FRONT RIGHT SIDE RIGHT
	th_rcs[3] = CreateThruster (_V(-1.8505,0, 24.306), _V(1,0,0), RCSTH3, RCS2,  VACRCS_ISP, NMLRCS_ISP, P_NML);//CM FRONT LEFT SIDE LEFT

	th_rcs[4] = CreateThruster (_V( 1.124,2.03,16.182), _V(0, 1,0), RCSTH4, RCS3,  VACRCS_ISP, NMLRCS_ISP, P_NML);//TRUSS FORWARD RIGHT SIDE UP
	th_rcs[5] = CreateThruster (_V( -1.124,2.03,16.182), _V(0,1,0), RCSTH5, RCS4,  VACRCS_ISP, NMLRCS_ISP, P_NML);//TRUSS FORWARD LEFT SIDE UP
	th_rcs[6] = CreateThruster (_V(1.124,-1.951,16.182), _V(0,-1,0), RCSTH6, RCS5,  VACRCS_ISP, NMLRCS_ISP, P_NML);//TRUSS FORWARD RIGHT SIDE DOWN
	th_rcs[7] = CreateThruster (_V(-1.124,-1.951,16.182), _V(0,-1,0), RCSTH7, RCS6,  VACRCS_ISP, NMLRCS_ISP, P_NML);//TRUSS FORWARD LEFT SIDE DOWN

	th_rcs[8] = CreateThruster (_V( 1.124,2.03, -16.125), _V(0,1,0), RCSTH8, RCS7,  VACRCS_ISP, NMLRCS_ISP, P_NML);//TRUSS AFT RIGHT SIDE UP
	th_rcs[9] = CreateThruster (_V( -1.124,2.03, -16.125), _V( 0,1,0), RCSTH9, RCS8,  VACRCS_ISP, NMLRCS_ISP, P_NML);//TRUSS AFT LEFT SIDE UP
	th_rcs[10] = CreateThruster (_V(1.124,-1.951,-16.125), _V(0,-1,0), RCSTH10, RCS9,  VACRCS_ISP, NMLRCS_ISP, P_NML);//TRUSS AFT RIGHT SIDE DOWN
	th_rcs[11] = CreateThruster (_V(-1.124,-1.951,-16.125), _V( 0,-1,0), RCSTH11, RCS10,  VACRCS_ISP, NMLRCS_ISP, P_NML);//TRUSS AFT LEFT SIDE DOWN

	th_rcs[12] = CreateThruster (_V( 1.89,0,-29.371), _V(0,0, -1), RCSTH12, RCS11,  VACRCS_ISP, NMLRCS_ISP, P_NML);//SM BACK RIGHT SIDE BACKWARDS
	th_rcs[13] = CreateThruster (_V( -1.89,0,-29.371), _V(0,0,-1), RCSTH13, RCS12,  VACRCS_ISP, NMLRCS_ISP, P_NML);//SM BACK LEFT SIDE BACKWARDS

	th_rcs[14] = CreateThruster (_V(2.22,0,-28.963), _V( -1,0,0), RCSTH14, RCS11,  VACRCS_ISP, NMLRCS_ISP, P_NML);//SM BACK RIGHT SIDE RIGHT
	th_rcs[15] = CreateThruster (_V(-2.22,0,-28.963), _V(1,0, 0), RCSTH15, RCS12,  VACRCS_ISP, NMLRCS_ISP, P_NML);//SM BACK LEFT SIDE LEFT

	SURFHANDLE texH2O2RCS = oapiRegisterExhaustTexture ("exhaust_atrcsShuttleD");

	AddExhaust (th_rcs[12], 1.9,  0.278, _V( 1.611,0,24.707), _V(0,0,1), texH2O2RCS);
	AddExhaust (th_rcs[13], 1.9,  0.278, _V( -1.611,0,24.707), _V(0,0,1), texH2O2RCS);

	AddExhaust (th_rcs[2], 1.9, 0.278, _V(1.8905,0, 24.306), _V(1, 0,0), texH2O2RCS);
	AddExhaust (th_rcs[3], 1.9, 0.278, _V(-1.8905,0, 24.306), _V(-1,0,0), texH2O2RCS);

	AddExhaust (th_rcs[6], 1.9,  0.278, _V( 1.124,2.03,16.182), _V(0, 1,0), texH2O2RCS);
	AddExhaust (th_rcs[7], 1.9,  0.278, _V( -1.124,2.03,16.182), _V(0,1,0), texH2O2RCS);
	AddExhaust (th_rcs[4], 1.9, 0.278, _V(1.124,-1.951,16.182), _V(0,-1,0), texH2O2RCS);
	AddExhaust (th_rcs[5], 1.9, 0.278, _V(-1.124,-1.951,16.182), _V(0,-1,0), texH2O2RCS);

	AddExhaust (th_rcs[10], 1.9,  0.278, _V( 1.124,2.03, -16.125), _V(0,1,0), texH2O2RCS);
	AddExhaust (th_rcs[11], 1.9,  0.278, _V( -1.124,2.03, -16.125), _V( 0,1,0), texH2O2RCS);
	AddExhaust (th_rcs[8], 1.9, 0.278, _V(1.124,-1.951,-16.125), _V(0,-1,0), texH2O2RCS);
	AddExhaust (th_rcs[9], 1.9, 0.278, _V(-1.124,-1.951,-16.125), _V( 0,-1,0), texH2O2RCS);

	AddExhaust (th_rcs[0], 1.9,  0.278, _V( 1.89,0,-29.371), _V(0,0, -1), texH2O2RCS);
	AddExhaust (th_rcs[1], 1.9,  0.278, _V( -1.89,0,-29.371), _V(0,0,-1), texH2O2RCS);

	AddExhaust (th_rcs[14], 1.9, 0.278, _V(2.22,0,-28.963), _V( 1,0,0), texH2O2RCS);
	AddExhaust (th_rcs[15], 1.9, 0.278, _V(-2.22,0,-28.963), _V(-1,0, 0), texH2O2RCS);


	th_group[0] = th_rcs[4];
	th_group[1] = th_rcs[5];
	th_group[2] = th_rcs[10];
	th_group[3] = th_rcs[11];
	CreateThrusterGroup (th_group, 4, THGROUP_ATT_PITCHUP);

	th_group[0] = th_rcs[6];
	th_group[1] = th_rcs[7];
	th_group[2] = th_rcs[8];
	th_group[3] = th_rcs[9];
	CreateThrusterGroup (th_group, 4, THGROUP_ATT_PITCHDOWN);

	th_group[0] = th_rcs[4];
	th_group[1] = th_rcs[7];
	th_group[2] = th_rcs[8];
	th_group[3] = th_rcs[11];
	CreateThrusterGroup (th_group, 4, THGROUP_ATT_BANKLEFT);

	th_group[0] = th_rcs[5];
	th_group[1] = th_rcs[6];
	th_group[2] = th_rcs[9];
	th_group[3] = th_rcs[10];
	CreateThrusterGroup (th_group, 4, THGROUP_ATT_BANKRIGHT);

	th_group[0] = th_rcs[4];
	th_group[1] = th_rcs[5];
	th_group[2] = th_rcs[8];
	th_group[3] = th_rcs[9];
	CreateThrusterGroup (th_group, 4, THGROUP_ATT_UP);

	th_group[0] = th_rcs[6];
	th_group[1] = th_rcs[7];
	th_group[2] = th_rcs[10];
	th_group[3] = th_rcs[11];
	CreateThrusterGroup (th_group, 4, THGROUP_ATT_DOWN);

	th_group[0] = th_rcs[2];
	th_group[1] = th_rcs[15];
	CreateThrusterGroup (th_group, 2, THGROUP_ATT_YAWLEFT);

	th_group[0] = th_rcs[3];
	th_group[1] = th_rcs[14];
	CreateThrusterGroup (th_group, 2, THGROUP_ATT_YAWRIGHT);

	th_group[0] = th_rcs[2];
	th_group[1] = th_rcs[14];
	CreateThrusterGroup (th_group, 2, THGROUP_ATT_LEFT);

	th_group[0] = th_rcs[3];
	th_group[1] = th_rcs[15];
	CreateThrusterGroup (th_group, 2, THGROUP_ATT_RIGHT);

	th_group[0] = th_rcs[0];
	th_group[1] = th_rcs[1];
	CreateThrusterGroup (th_group, 2, THGROUP_ATT_FORWARD);

	th_group[0] = th_rcs[12];
	th_group[1] = th_rcs[13];
	CreateThrusterGroup (th_group, 2, THGROUP_ATT_BACK);

	Dock0 = CreateDock(_V(0,-0.737,25.385),_V(0,0,1),_V(0,1,0));

}

//=========================================================
// clbkMFDMode
// Required for proper use of MFDs in the virtual cockpit. Beyond that, I know very little about what this part does.
//=========================================================

void ShuttleD::clbkMFDMode (int mfd, int mode)
{
	//	When an MFD changes mode (either in Panel or VC modes), this call back is
	//	invoked. Here, it is a general TriggerRedrawArea function used, which can
	//	be used to redraw MFD's for VC or Panel views. You can just as effectively
	//	use oapiTriggerVCRedrawArea for this ship, however, as there is no 2D panel.
	switch (mfd) {
	case MFD_LEFT:
		oapiTriggerRedrawArea (1, 2, AID_MFD1_LBUTTONS);
		oapiTriggerRedrawArea (1, 3, AID_MFD1_RBUTTONS);
		break;
		case MFD_RIGHT:
		oapiTriggerRedrawArea (1, 2, AID_MFD2_LBUTTONS);
		oapiTriggerRedrawArea (1, 3, AID_MFD2_RBUTTONS);
	}
}

//=========================================================
// clbkLoadVC
// This is where most code for the VC is added. oapiVCRegisterMFD & VCHUDSPEC are used to "create" the MFDs and the Heads-up display. After that,
// the code is subdivided into "case" sections, each one representing a different camera view in the virtual cockpit. The key to switching between these
// is the oapiVCSetNeighbours (a, b, c, d) function. All you need to do is remember a=left, b=right, c=up, d=down, and place the id # of the campos you
// want to switch to in place of that letter when the user its ctrl-directional arrow. -1 specifies a null value (camera wont move) and thats about all
// you need to know to have multiple VC camera points.
//=========================================================

bool ShuttleD::clbkLoadVC (int id)
{

	//	 VCHUDSPEC hud;
	VCMFDSPEC mfd;

	oapiVCRegisterMFD(0,&mfd);
	oapiVCRegisterMFD(1,&mfd);
	static VCHUDSPEC hud_pilot  = {1, 4,{0.55,1.90,26.5162},2.62999};
	static VCHUDSPEC hud_copilot  = {1, 4,{-0.55,1.90,26.5162},2.62999};
	static VCHUDSPEC hud_fdeckfloor  = {1, 4,{0.55,1.90,26.5162},2.62999};
	//	oapiVCRegisterHUD (&hud_pilot); // HUD parameters

	mfds_right.nmesh = 1;	//	The mesh number (the first one loaded, or 0, in this case).
	mfds_right.ngroup  = 19;	//	The mesh group that is the MFD screen in the above identified mesh.

	mfds_left.nmesh = 1;	//	The mesh number (the first one loaded, or 0, in this case).
	mfds_left.ngroup  = 18;	//	The mesh group that is the MFD screen in the above identified mesh.

	SetCameraDefaultDirection(_V(0, 0, 1)); // View angles down so you can see the
	//	MFD in VC view by default (it is the sine and cosine of 11º in Y and Z, respectively).

	SURFHANDLE MFDbuttons1 = oapiGetTextureHandle (DVCInterior,12); 
	//	Get the MFDButtons.dds D texture for redrawing purposes.

	int i;
	switch (id) {

	case 0:	//	The first VC cockpit view (id 0). Carefull with mixing up id.
		//	There are more references to id in the code, but what they identify is 
		//	according to the specific callback. For example, in clbkVCMouseEvent,
		//	id is the identity of the MFD buttons, not the cockpit view. 
		SetCameraOffset (_V(0.50+(0.8*sin(Randomizer)),1.94+(0.8*sin(Randomizer)),22.86));
		SetCameraRotationRange (RAD*120, RAD*120, RAD*180, RAD*100); 
		SetCameraShiftRange (_V(0,0.1,0.2), _V(-0.2,0,0), _V(0.05,0.22,0));
		oapiVCSetNeighbours (1, -1, -1, 2);

		oapiVCRegisterMFD (MFD_LEFT, &mfds_left);
		oapiVCRegisterMFD (MFD_RIGHT, &mfds_right);
		oapiVCRegisterHUD (&hud_pilot); // HUD parameters


		oapiVCRegisterArea (AID_GEARDOWNSWITCH, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);

		oapiVCSetAreaClickmode_Spherical(AID_GEARDOWNSWITCH, _V(-0.06900, 2.5373, 23.7297), 0.0140);

		oapiVCRegisterArea (AID_GEARUPSWITCH, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);

		oapiVCSetAreaClickmode_Spherical(AID_GEARUPSWITCH, _V(-0.06900, 2.5637, 23.7176), 0.0140);

		oapiVCRegisterArea (AID_PLBAYACLOSESWITCH, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);

		oapiVCSetAreaClickmode_Spherical(AID_PLBAYACLOSESWITCH, _V(0.04784, 2.5373, 23.7297), 0.0140);

		oapiVCRegisterArea (AID_PLBAYAOPENSWITCH, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);

		oapiVCSetAreaClickmode_Spherical(AID_PLBAYAOPENSWITCH, _V(0.04784, 2.5637, 23.7176), 0.0140);

		oapiVCRegisterArea (AID_PLBAYBCLOSESWITCH, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);

		oapiVCSetAreaClickmode_Spherical(AID_PLBAYBCLOSESWITCH, _V(0.16468, 2.5373, 23.7297), 0.0140);

		oapiVCRegisterArea (AID_PLBAYBOPENSWITCH, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);

		oapiVCSetAreaClickmode_Spherical(AID_PLBAYBOPENSWITCH, _V(0.16468, 2.5637, 23.7176), 0.0140);


		oapiVCRegisterArea (AID_MFD1_LBUTTONS, _R( 0, 0, 32, 220), PANEL_REDRAW_USER, PANEL_MOUSE_IGNORE, PANEL_MAP_BACKGROUND, MFDbuttons1);		

		oapiVCRegisterArea (AID_MFD1_RBUTTONS, _R( 32, 0, 64, 220), PANEL_REDRAW_USER, PANEL_MOUSE_IGNORE, PANEL_MAP_BACKGROUND, MFDbuttons1);

		oapiVCRegisterArea (AID_MFD2_LBUTTONS, _R( 0, 0, 32, 220), PANEL_REDRAW_USER, PANEL_MOUSE_IGNORE, PANEL_MAP_BACKGROUND, MFDbuttons1);		

		oapiVCRegisterArea (AID_MFD2_RBUTTONS, _R( 32, 0, 64, 220), PANEL_REDRAW_USER, PANEL_MOUSE_IGNORE, PANEL_MAP_BACKGROUND, MFDbuttons1);

		for (i = 0; i < 6; i++) {
			oapiVCRegisterArea (MFD1_LBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD1_LBUTTON1+i, _V(-0.07125, 2.4464 - (i *0.0352), 23.77135 + (i *0.0161)), 0.0100);

			oapiVCRegisterArea (MFD1_RBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD1_RBUTTON1+i, _V(0.25895, 2.4464 - (i *0.0352), 23.77135 + (i *0.0161)), 0.0300);
		}
		//	Define the area for mouse events on each button of left and right columns.

		for (i = 0; i < 3; i++) {
			oapiVCRegisterArea (MFD1_BBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD1_BBUTTON1+i, _V(0.05295 + (i * 0.0387), 2.2081, 23.87965), 0.0300);
		}

				for (i = 0; i < 6; i++) {
			oapiVCRegisterArea (MFD2_LBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD2_LBUTTON1+i, _V(0.27325, 2.4464 - (i *0.0352), 23.77135 + (i *0.0161)), 0.0300);

			oapiVCRegisterArea (MFD2_RBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD2_RBUTTON1+i, _V(0.60355, 2.4464 - (i *0.0352), 23.77135 + (i *0.0161)), 0.0300);
		}
		//	Define the area for mouse events on each button of left and right columns.

		for (i = 0; i < 3; i++) {
			oapiVCRegisterArea (MFD2_BBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD2_BBUTTON1+i, _V(0.3975 + (i * 0.0387), 2.2081, 23.87965), 0.0300);
		}
		//	Define the area for mouse events on the bottom buttons. Coordinates are in
		//	mesh 3D coords (see why you needed to get this info?)

		campos = CAM_VCPILOT;
		break;



	case 1: // Pilots seat
		SetCameraOffset (_V(-0.55,1.94,22.86));
		SetCameraRotationRange (RAD*120, RAD*120, RAD*100, RAD*180); 
		SetCameraShiftRange (_V(0,0,0.7), _V(-0.05,0.2,0), _V(0.2,0,0));
		oapiVCSetNeighbours (-1, 0, -1, 2);
		oapiVCRegisterHUD (&hud_copilot); // HUD parameters

		oapiVCRegisterMFD (MFD_LEFT, &mfds_left);
		oapiVCRegisterMFD (MFD_RIGHT, &mfds_right);

				for (i = 0; i < 6; i++) {
			oapiVCRegisterArea (MFD1_LBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD1_LBUTTON1+i, _V(-0.07125, 2.4464 - (i *0.0352), 23.77135 + (i *0.0161)), 0.0300);

			oapiVCRegisterArea (MFD1_RBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD1_RBUTTON1+i, _V(0.25895, 2.4464 - (i *0.0352), 23.77135 + (i *0.0161)), 0.0300);
		}
		//	Define the area for mouse events on each button of left and right columns.

		for (i = 0; i < 3; i++) {
			oapiVCRegisterArea (MFD1_BBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD1_BBUTTON1+i, _V(0.05295 + (i * 0.0387), 2.2081, 23.87965), 0.0300);
		}

				for (i = 0; i < 6; i++) {
			oapiVCRegisterArea (MFD2_LBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD2_LBUTTON1+i, _V(0.29325, 2.4464 - (i *0.0352), 23.77135 + (i *0.0161)), 0.0300);

			oapiVCRegisterArea (MFD2_RBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD2_RBUTTON1+i, _V(0.62355, 2.4464 - (i *0.0352), 23.77135 + (i *0.0161)), 0.0300);
		}
		//	Define the area for mouse events on each button of left and right columns.

		for (i = 0; i < 3; i++) {
			oapiVCRegisterArea (MFD2_BBUTTON1+i, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
			oapiVCSetAreaClickmode_Spherical(MFD2_BBUTTON1+i, _V(0.4175 + (i * 0.0387), 2.2081, 23.87965), 0.0300);
		}
		//	Define the area for mouse events on the bottom buttons. Coordinates are in
		//	mesh 3D coords

		campos = CAM_VCPSNGR1;
		break;

	case 2: // Flight Deck Floor
		SetCameraOffset (_V(0,0.3,22.86));
		SetCameraRotationRange (RAD*180, RAD*70, RAD*100, RAD*100); 
		SetCameraShiftRange (_V(0,0.7,0.1), _V(-0.05,0,0), _V(0.05,0,0));
		oapiVCSetNeighbours (1, 0, -1, 3);
		oapiVCRegisterHUD (&hud_fdeckfloor); // HUD parameters
		oapiVCRegisterMFD (MFD_LEFT, &mfds_left);
		oapiVCRegisterMFD (MFD_RIGHT, &mfds_right);
		campos = CAM_VCPSNGR2;
		break;

	case 3: // Hab tunnel 1
		SetCameraOffset (_V(0, 0.1, -2.01));
		SetCameraRotationRange (RAD*180, RAD*70, RAD*270, RAD*100); 
		SetCameraShiftRange (_V(0,0.3,24.87), _V(-0.05,0,0), _V(0.05,0,0));
		oapiVCSetNeighbours (-1, -1, 2, 4);
		campos = CAM_VCPSNGR3;
		break;

	case 4: // Hab tunnel 2
		SetCameraOffset (_V(0, 0.1, -16.01));
		SetCameraRotationRange (RAD*180, RAD*180, RAD*180, RAD*180); 
		SetCameraShiftRange (_V(0,0,14.0), _V(-0.05,0,0), _V(0.05,0,0));
		oapiVCSetNeighbours (-1, -1, 3, -1);
		campos = CAM_VCPSNGR4;
		return true;

	};

	return true;
}

//=========================================================
// clbkDrawHUD
// This code helps in drawing the custom hud displays that UMMU & UCGO use to send messages. All I really know about this at this point is that in
//   5,hps->H/60*25,cUmmuHudDisplay 
// 60 & 25 appear to change where the text appears on the display.
//=========================================================

bool ShuttleD::clbkDrawHUD(int mode, const HUDPAINTSPEC *hps, oapi::Sketchpad *skp)
{
	// draw the default HUD
	VESSEL3::clbkDrawHUD (mode, hps, skp);

	// UMmu display messages
	if(dHudMessageDelay>0)
	{
		skp->Text(5,hps->H/60*25,cUmmuHudDisplay,strlen(cUmmuHudDisplay));
		dHudMessageDelay-=oapiGetSimStep();
		if(dHudMessageDelay<0)
			dHudMessageDelay=0;
	}
	// UCGO display messages
	if(dCargHudMessageDelay>0)
	{
		skp->Text(5,hps->H/60*20,cCargoHudDisplay,strlen(cCargoHudDisplay));
		dCargHudMessageDelay-=oapiGetSimStep();
		if(dCargHudMessageDelay<0)
			dCargHudMessageDelay=0;
	}
	return true; 
}
char *ShuttleD::SendHudMessage() //<---- Change the class name here
{
	dHudMessageDelay=15;
	return cUmmuHudDisplay;
}

//=========================================================
// clbkVCMouseEvent
// clbkVCMouseEvent acts as a sort of organizer for VC switch actions. Basically, when you create a VC mouseclick area up in clbkLoadVC, you give it an
// id like AID_GEARDOWNSWITCH, then whenever it gets called (button pressed), whatever function you list here gets called, in this case Geardown()
//=========================================================

bool ShuttleD::clbkVCMouseEvent (int id, int event, VECTOR3 &p)
{
	if ((id) >= MFD1_LBUTTON1 && (id) < MFD1_LBUTTON1 + 12)
	{
		oapiProcessMFDButton (MFD_LEFT, id - MFD1_LBUTTON1, event);
		return true;
	}
	//	Processes the events for mouse clicks on VC view MFD buttons, left and
	//	right columns, by matching the id of the event to the #defined identifier of
	//	the button.

	if ((id) == MFD1_BBUTTON1)
	{
		oapiToggleMFD_on (MFD_LEFT);
		return true;
	}
	//	The ON / OFF button (left MFD button on the bottom).

	if ((id) == MFD1_BBUTTON2)
	{
		oapiSendMFDKey (MFD_LEFT, OAPI_KEY_F1);
		return true;
	}
	//	The MODE button (center MFD button on the bottom).

	if ((id) == MFD1_BBUTTON3)
	{
		oapiSendMFDKey (MFD_LEFT, OAPI_KEY_GRAVE);
		return true;
	}
	//	The MENU button (right MFD button on the bottom).

		if ((id) >= MFD2_LBUTTON1 && (id) < MFD2_LBUTTON1 + 12)
	{
		oapiProcessMFDButton (MFD_RIGHT, id - MFD2_LBUTTON1, event);
		return true;
	}
	//	Processes the events for mouse clicks on VC view MFD buttons, left and
	//	right columns, by matching the id of the event to the #defined identifier of
	//	the button (see AstroMatiz.h file).

	if ((id) == MFD2_BBUTTON1)
	{
		oapiToggleMFD_on (MFD_RIGHT);
		return true;
	}
	//	The ON / OFF button (left MFD button on the bottom).

	if ((id) == MFD2_BBUTTON2)
	{
		oapiSendMFDKey (MFD_RIGHT, OAPI_KEY_F1);
		return true;
	}
	//	The MODE button (center MFD button on the bottom).

	if ((id) == MFD2_BBUTTON3)
	{
		oapiSendMFDKey (MFD_RIGHT, OAPI_KEY_GRAVE);
		return true;
	}
	//	The MENU button (right MFD button on the bottom).

	if ((id) == AID_GEARDOWNSWITCH)
	{
		Geardown();
		return true;
	}

		if ((id) == AID_GEARUPSWITCH)
	{
		Gearup();
		return true;
	}

		if ((id) == AID_PLBAYAOPENSWITCH)
	{
		MainBayOpen();
		return true;
	}

		if ((id) == AID_PLBAYACLOSESWITCH)
	{
		MainBayClose();
		return true;
	}

		if ((id) == AID_PLBAYBOPENSWITCH)
	{
		AuxBayOpen();
		return true;
	}

		if ((id) == AID_PLBAYBCLOSESWITCH)
	{
		AuxBayClose();
		return true;
	}

	return false;
}

//=========================================================
// clbkVCRedrawEvent
// Here there be dragons ;). The only thing I know about this is that it seems to be related to the text normally drawn onto a MFDs side switches.
// That text did appear on various mesh groups in the VC during development, but I never could properly track it down & get it where I wanted it.
// Maybe I can get it working in a future release.
//=========================================================

bool ShuttleD::clbkVCRedrawEvent (int id, int event, SURFHANDLE surf)
{
	int bt, side;
	switch (id) {
		const char *label;
	case AID_MFD1_LBUTTONS:
	case AID_MFD1_RBUTTONS:
		side = (id == AID_MFD1_LBUTTONS ? 0: 1);
		HDC hDC = oapiGetDC (surf);
		SelectObject (hDC, hFont);
		SetTextColor (hDC, RGB(250, 250, 100));
		SetTextAlign (hDC, TA_CENTER);
		SetBkMode (hDC, TRANSPARENT);
		for (bt = 0; bt < 6; bt++) {
			if (label = oapiMFDButtonLabel (MFD_LEFT, bt+side*6))
				TextOut (hDC, 16, 8+36*bt, label, strlen(label));
			else break;
		}


		oapiReleaseDC (surf, hDC);
		return true;
	}
	return false;

};

//=========================================================
// clbkLoadStateEx
// I would like to dedicate this as the Face function, lol. A big thanks to Face who helped me fix this part of the code when it wasnt written properly,
// and was causing me a lot of grief. The key thing to remember here and in clbkSaveState is that when custom variables (data about your ship) gets saved
// it gets a line in the SCN file. In say,
//
//		if (!_strnicmp (line, "O2Tank", 6)) {
//			sscanf (line+6, "%lf", &O2Tank);
//		}
//
// The number 6 refers to the number of characters in O2Tank - 6. This is the length of the id string, NOT how many lines it has to slip down in the
// scn file.
//=========================================================
void ShuttleD::clbkLoadStateEx (FILEHANDLE scn, void *status)
{
	char *line;
	while (oapiReadScenario_nextline (scn, line)) 
	{

		if (!_strnicmp (line, "GEAR", 4)) {
			sscanf (line+4, "%d%lf", &GEAR_status, &GEAR_proc);
		}

		if (!_strnicmp (line, "PLBAYA", 6)) {
			sscanf (line+6, "%d%lf", &PLBAYA_status, &PLBAYA_proc);
		}

		if (!_strnicmp (line, "PLBAYB", 6)) {
			sscanf (line+6, "%d%lf", &PLBAYB_status, &PLBAYB_proc);
		}

		if (!_strnicmp (line, "O2Tank", 6)) {
			sscanf (line+6, "%lf", &O2Tank);
		}

		if(Crew.LoadAllMembersFromOrbiterScenario(line)==TRUE)
			continue;

		// Load UCGO 2.0 cargo from scenario
		if(hUcgo.LoadCargoFromScenario(line)==TRUE) // UCGO load cargo 
			continue;

		ParseScenarioLineEx (line, status);
	}
	SetAnimation (anim_gear, GEAR_proc);
	SetAnimation (anim_PLBAYA, PLBAYA_proc);
	SetAnimation (anim_PLBAYB, PLBAYB_proc);

}

//=========================================================
// clbkSaveState
// Nothing too exciting here, just the same rules as above, but it is worth noting that in "%d %0.4f", the d & f appear to specify
// the data type being saved. In other words, the specifier "%d" or "%0.4f" will determine how many zeroes get written to the SCN, &
// as a result remembered.
//=========================================================

void ShuttleD::clbkSaveState (FILEHANDLE scn)
{
	char cbuf[256];

	VESSEL3::clbkSaveState (scn);
	sprintf (cbuf, "%d %0.4f", GEAR_status, GEAR_proc);
	oapiWriteScenario_string (scn, "GEAR", cbuf);

	sprintf (cbuf, "%d %0.4f", PLBAYA_status, PLBAYA_proc);
	oapiWriteScenario_string (scn, "PLBAYA", cbuf);

	sprintf (cbuf, "%d %0.4f", PLBAYB_status, PLBAYB_proc);
	oapiWriteScenario_string (scn, "PLBAYB", cbuf);

	sprintf (cbuf, "%0.4f", O2Tank);
	oapiWriteScenario_string (scn, "O2Tank", cbuf);

	Crew.SaveAllMembersInOrbiterScenarios(scn);

	// Save UCGO 2.0 cargo in scenario
	hUcgo.SaveCargoToScenario(scn);	
}

//=========================================================
// clbkVisualCreated
// Clear as mud to me, but I think UCGO uses it when adding the attached cargo meshes to the simulation. Dansteph probably knows...
//=========================================================

void ShuttleD::clbkVisualCreated (VISHANDLE vis, int refcount)
{
	hUcgo.SetUcgoVisual(vis);	// must be called in clbkVisualCreated.
}

//=========================================================
// SendCargHudMessage
// Another UCGO specific function, which you would think would be related to the hud.
//=========================================================

char *ShuttleD::SendCargHudMessage(void)
{
	dCargHudMessageDelay=15; // 15 seconds display delay for msg
	return cCargoHudDisplay;
}

//=========================================================
// clbkPostStep
// This is the step-to-step, always-in-motion part of the code during an Orbiter simulation. Orbiter does physics caculations every timestep,
// and as a result, interesting things can be done here like constant updating of variables, the nuts & bolts that drive the animations up & down,
// as well as functions to kill the crew when crashing or running out of air. I wont go into great detail on what I have in here, but this tends to be
// the exciting part of the code. This is where things happen!
//=========================================================

void ShuttleD::clbkPostStep(double simt, double simdt, double mjd)
{

SetEmptyMass(UpdateMass());
hUcgo.UpdateEmptyMass();

	int ReturnCode=Crew.ProcessUniversalMMu();
	switch(ReturnCode)
	{
	case UMMU_TRANSFERED_TO_OUR_SHIP: 
		sprintf(SendHudMessage(),"%s \"%s\" transfered to %s",
			Crew.GetCrewMiscIdByName(Crew.GetLastEnteredCrewName()),Crew.GetLastEnteredCrewName()
			,GetName());
		break;
	case UMMU_RETURNED_TO_OUR_SHIP:
		sprintf(SendHudMessage(),"%s \"%s\" ingressed %s",
			Crew.GetCrewMiscIdByName(Crew.GetLastEnteredCrewName()),
			Crew.GetLastEnteredCrewName(),GetName());
		break;
	}

		int ActionAreaReturnCode=Crew.DetectActionAreaActivated();
	if(ActionAreaReturnCode>-1)
	{
		// this is just an example, we have four area declared
		// action area ID 0 triggered 
		if(ActionAreaReturnCode==0)
		{
		RevertPLBAYA();
		}
		// action area ID 1 triggered
		else if(ActionAreaReturnCode==1)
		{
		RevertPLBAYB();
		}
		// action area ID 2 triggered
		else if(ActionAreaReturnCode==2)
		{
		// switch state
		Crew.SetAirlockDoorState(!Crew.GetAirlockDoorState());
		// display state
		if(Crew.GetAirlockDoorState()==TRUE)
			strcpy(SendHudMessage(),"Airlock open");	
		else
			strcpy(SendHudMessage(),"Airlock closed");	
		}
	}

	if (GEAR_status >= GEAR_RAISING) { 
		double da = simdt * GEAR_OPERATING_SPEED;
		if (GEAR_status == GEAR_RAISING) {
			if (GEAR_proc > 0.0) GEAR_proc = max (0.0, GEAR_proc-da);
			else                GEAR_status = GEAR_UP;
		} else {
			if (GEAR_proc < 1.0) GEAR_proc = min (1.0, GEAR_proc+da);
			else                GEAR_status = GEAR_DOWN;
		}
		SetAnimation (anim_gear, GEAR_proc);

	}

	SetTouchdownPoints (_V(0,-4.89+GEAR_proc*0.99,1), _V(-1,-4.89+GEAR_proc*0.99,-1), _V(1,-4.89+GEAR_proc*0.99,-1));

	if (PLBAYA_status >= PLBAYA_CLOSING) {
		double da = simdt * PLBAYA_OPERATING_SPEED;
		if (PLBAYA_status == PLBAYA_CLOSING) {
			if (PLBAYA_proc > 0.0) PLBAYA_proc = max (0.0, PLBAYA_proc-da);
			else                PLBAYA_status = PLBAYA_UP;
		} else {
			if (PLBAYA_proc < 1.0) PLBAYA_proc = min (1.0, PLBAYA_proc+da);
			else                PLBAYA_status = PLBAYA_DOWN;
		}
		SetAnimation (anim_PLBAYA, PLBAYA_proc);



	}

	if (PLBAYB_status >= PLBAYB_CLOSING) {
		double da = simdt * PLBAYB_OPERATING_SPEED;
		if (PLBAYB_status == PLBAYB_CLOSING) {
			if (PLBAYB_proc > 0.0) PLBAYB_proc = max (0.0, PLBAYB_proc-da);
			else                PLBAYB_status = PLBAYB_UP;
		} else {
			if (PLBAYB_proc < 1.0) PLBAYB_proc = min (1.0, PLBAYB_proc+da);
			else                PLBAYB_status = PLBAYB_DOWN;
		}
		SetAnimation (anim_PLBAYB, PLBAYB_proc);



	}

	if (GEAR_status == GEAR_RAISING) {
		{if(GEAR_proc > 0.99)
			PlayVesselWave(SHD,GEARDOWN);
		}
	}

	if (GEAR_status == GEAR_LOWERING) {
		{if(GEAR_proc < 0.01)
			PlayVesselWave(SHD,GEARUP);
		}
	}
	if (PLBAYA_status == PLBAYA_CLOSING) {
		{if(PLBAYA_proc > 0.99)
			PlayVesselWave(SHD,PLBAYACLOSE);
		}
	}

	if (PLBAYA_status == PLBAYA_OPENING) {
		{if(PLBAYA_proc < 0.01)
			PlayVesselWave(SHD,PLBAYAOPEN);
		}
	}
	if (PLBAYB_status == PLBAYB_CLOSING) {
		{if(PLBAYB_proc > 0.9)
			PlayVesselWave(SHD,PLBAYBCLOSE);
		}
	}

	if (PLBAYB_status == PLBAYB_OPENING) {
		{if(PLBAYB_proc < 0.1)
			PlayVesselWave(SHD,PLBAYBOPEN);
		}
	}

	if(GroundContact()==TRUE)
	{
		// we check vertical speed
		int I;
		VECTOR3 vHorizonAirspeedVector={0};
		GetHorizonAirspeedVector (vHorizonAirspeedVector);
		double VertSpeed		=vHorizonAirspeedVector.y;
		if(VertSpeed<-3)
		{
			// we touched ground with more than -3 m/s, sorry dude, time to kill you all :(
			for(I=0;I<Crew.GetCrewTotalNumber();I++)
			{
				Crew.SetCrewMemberPulseBySlotNumber(I,0);	// set cardiac pulse to zero
			}
			strcpy(SendHudMessage(),"Crashed into terrain");
		}
	}

		// Detect Atmospheric Entry-Function Courtesy of Hlynkacg
	if ( (GetDynPressure() > 44000) ) // It's about to get very hot in here...
	{
			int I;
			// This procedure wasnt in the owners manual... What do you mean we dont have a heat shield?!?!?
			for(I=0;I<Crew.GetCrewTotalNumber();I++)
			{
				Crew.SetCrewMemberPulseBySlotNumber(I,0);	// set cardiac pulse to zero
			}
			strcpy(SendHudMessage(),"Hull Breach due to Atmospheric Reentry");
	}


	if (PLBAYA_status == PLBAYA_UP) {
		hUcgo.SetSlotDoorState(FALSE);
	}

	if (PLBAYA_status == PLBAYA_DOWN) {
		hUcgo.SetSlotDoorState(TRUE);
	}

	if (PLBAYB_status == PLBAYB_OPENING) {
		{if(PLBAYB_proc < 0.1)
			PlayVesselWave(SHD,PLBAYBOPEN);
		}
	}

	double CrewNumber = Crew.GetCrewTotalNumber();
	double OxygenConsumption = 1.2*1.157407E-5*CrewNumber;
	double OxygenLevel = O2Tank;

{
	if (O2Tank>0)
	{
	O2Tank = (O2Tank - OxygenConsumption*simdt);
	}
	else 
	{
	O2Tank=0;
	}
}
	int I;
	if(O2Tank == 0)
{
			// You ran out of oxygen, sorry dude, time to kill you all :(
			for(I=0;I<Crew.GetCrewTotalNumber();I++)
			{
				Crew.SetCrewMemberPulseBySlotNumber(I,0);	// set cardiac pulse to zero
		}
		strcpy(SendHudMessage(),"O2 Main Tank empty-All crew dead");
}

Randomizer = simdt;

		AddUMmuToVessel();

		Crew.WarnUserUMMUNotInstalled("Shuttle-D");
		// At the very end of clbkPostStep or clbkPreStep
		hUcgo.WarnUserUCGONotInstalled("Shuttle-D");
}


//=========================================================
// clbkConsumeBufferedKey
// Another important function, this particular section is used to execute functions whenever the Orbiter application detects a given keystroke by a user.
// Really not very complicated (usually), very similar to clbkVCMouseEvent. The ony complicated part is getting the structure right, as calls specific
// to a ctrl-keypress or shift-keypress have to be grouped under a separate heading. Best example of that will be under the Hubble Space Telescope sample
// also in the OrbiterSDK directory.
//=========================================================

int ShuttleD::clbkConsumeBufferedKey (DWORD key, bool down, char *kstate)
{

	if (!down) return 0;       // only process keydown events

	if(key==OAPI_KEY_G)
	{ //Gear
		RevertGEAR();
		return 1;
	}  
	if(key==OAPI_KEY_K)
	{// Payload Bay B
		RevertPLBAYB();
		return 1;
	}  
	if(key==OAPI_KEY_O)
	{// Payload Bay A
		RevertPLBAYA();
		return 1;
	} 

	if(key==OAPI_KEY_E&&!KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{
		// PERFORM THE EVA, first we get is name with "GetCrewNameBySlotNumber" then we perform EVA with "EvaCrewMember"
		int Returned=Crew.EvaCrewMember(Crew.GetCrewNameBySlotNumber(SelectedUmmuMember));
		// we provide feedback to user (You can display a message on panel or wathewer)
		// here below all the return code possible:
		switch(Returned)
		{
		case TRANSFER_TO_DOCKED_SHIP_OK:
			sprintf(SendHudMessage(),"%s transfered through main hatch",
				Crew.GetLastEvaedCrewName());SelectedUmmuMember=0;
			break;
		case EVA_OK:
			sprintf(SendHudMessage(),"%s on EVA",
				Crew.GetLastEvaedCrewName());SelectedUmmuMember=0;
			break;
		case ERROR_AIRLOCK_CLOSED:
			strcpy(SendHudMessage(),"Airlock closed. press A to open");
			break;
		case ERROR_DOCKED_SHIP_HAVE_AIRLOCK_CLOSED:
			strcpy(SendHudMessage(),"Docked vessel airlock closed");
			break;
		case ERROR_CREW_MEMBER_NOT_FOUND:
			strcpy(SendHudMessage(),"No crew by this name aboard");
			break;
		case ERROR_DOCKEDSHIP_DONOT_USE_UMMU:
			strcpy(SendHudMessage(),"Docked ship is not compatible with UMmu 2.0");
			break;
		case ERROR_MISC_ERROR_EVAFAILED:
			strcpy(SendHudMessage(),"Misc error with UMMU. Please reinstall");
			break;
		}
		return TRUE;
	}

	//---------------------------------------------------------------------------
	// Ummu Key "1" - Select next member This is just internal to the demo
	// you may do your own selection system by panel button, name etc etc
	if(key==OAPI_KEY_1&&!KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{
		// we test there is someone aboard
		if(Crew.GetCrewTotalNumber()==0)
		{
			strcpy(SendHudMessage(),"No crew aboard");	
			return 1;
		}

		// we test that we select existing member
		if(SelectedUmmuMember<Crew.GetCrewTotalNumber()-1)
			SelectedUmmuMember++;
		char * Name=Crew.GetCrewNameBySlotNumber(SelectedUmmuMember);
		sprintf(SendHudMessage(),"%i  %s \"%s\"Selected for EVA or Transfer",
			SelectedUmmuMember,Crew.GetCrewMiscIdBySlotNumber(SelectedUmmuMember),
			Name);
		return 1;
	}

	//---------------------------------------------------------------------------
	// Ummu Key "2" - Select previous member This is just internal to the demo
	// you may do your own selection system by panel button
	if(key==OAPI_KEY_2&&!KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{
		// we test there is someone aboard
		if(Crew.GetCrewTotalNumber()==0)
		{
			strcpy(SendHudMessage(),"No crew aboard");	
			return 1;
		}
		if(SelectedUmmuMember>0)
			SelectedUmmuMember--;
		char * Name=Crew.GetCrewNameBySlotNumber(SelectedUmmuMember);
		sprintf(SendHudMessage(),"Slot %i %s \"%s\" Selected for EVA or Transfer"
			", please press \"E\" to EVA",SelectedUmmuMember,
			Crew.GetCrewMiscIdBySlotNumber(SelectedUmmuMember),Name);
		return 1;
	}


		//---------------------------------------------------------------------------
	// UCGO Key "4" Select next Cargo Slot 
	if(key==OAPI_KEY_4&&!KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{


		// we test that we select existing member
		if(iSelectedCargo<18-1)
			iSelectedCargo++;
		double GetCargoSlotMass(int Num); 
		sprintf(SendCargHudMessage(),"Cargo Slot %i selected",
			iSelectedCargo);
		return 1;
	}

	//---------------------------------------------------------------------------
	// UCGO Key "3" Select previous Cargo Slot
	if(key==OAPI_KEY_3&&!KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{

		if(iSelectedCargo>0)
			iSelectedCargo--;
		double GetCargoSlotMass(int Num); 
		sprintf(SendCargHudMessage(),"Cargo Slot %i selected",
			iSelectedCargo);
		return 1;
	}

	//---------------------------------------------------------------------------
	// Ummu Key "A" Open & Close the virtual UMMU airlock door
	if(key==OAPI_KEY_A&&!KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{
		// switch state
		Crew.SetAirlockDoorState(!Crew.GetAirlockDoorState());
		// display state
		if(Crew.GetAirlockDoorState()==TRUE)
			strcpy(SendHudMessage(),"Airlock open");	
		else
			strcpy(SendHudMessage(),"Airlock closed");	
		return 1;
	}

	//---------------------------------------------------------------------------
	// Get some infos Name of ship and total soul aboard
	if(key==OAPI_KEY_0)
	{
		sprintf(SendHudMessage(),"%i crew aboard %s",
			Crew.GetCrewTotalNumber(),GetName());
		return 1;
	}

	// THIS IS FOR ADDING CREW SEE PDF doc "Allow user to add crew to your ship 
	// without scenery editor"
	if(key==OAPI_KEY_M&&!KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{
		AddUMmuToVessel(TRUE);
	}
	// 9 key "select" one cargo on disk (cycle)
	if(key==OAPI_KEY_9&&!KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{
		sprintf(SendCargHudMessage(),"%s",hUcgo.ScnEditor_SelectNextCargoAvailableOnDisk());
		return 1;
	}
	// SHIFT+9 key "add last cargo selected by key 9"
	// If iSelectedCargo=-1 (default) add to the first free slot found
	if(key==OAPI_KEY_9&&KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{
		if(hUcgo.ScnEditor_AddLastSelectedCargoToSlot(iSelectedCargo)==TRUE)
		{
			strcpy(SendCargHudMessage(),"Cargo loaded");
		}
		else
		{
			if(iSelectedCargo<0)
			{
				strcpy(SendCargHudMessage(),"Unable to load Cargo");
			}
			else
			{
				strcpy(SendCargHudMessage(),"Unable to load Cargo");
			}
		}
		return 1;
	}
	// "C" grapple cargo. If iSelectedCargo=-1 (default) add to the first free slot found
	if(key==OAPI_KEY_C&&!KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{
		int ReturnedCode=hUcgo.GrappleOneCargo(iSelectedCargo);
		// for return code list see function "GrappleOneCargo" in the header
		switch(ReturnedCode)
		{
		case 1:
			strcpy(SendCargHudMessage(),"Cargo grappled");
			break;
		case 0:
			strcpy(SendCargHudMessage(),"No cargo in range");
			break;
		case -1:
			strcpy(SendCargHudMessage(),"Maximum payload mass exceeded");
			break;
		case -2:
			strcpy(SendCargHudMessage(),"bad config, mesh not found or slot not declared");
			break;
		case -3:
			strcpy(SendCargHudMessage(),"Cargo slot full, unable to grapple cargo");
			break;
		case -4:
			strcpy(SendCargHudMessage(),"Payload Bay A closed");
			break;
		case -5:
			strcpy(SendCargHudMessage(),"Unable to load Cargo. All slots full");
			break;
		default:
			strcpy(SendCargHudMessage(),"Misc error. Unable to grapple cargo");
		}
		return 1;
	}
	// SHIFT+C release cargo. If iSelectedCargo=-1 (default) release the first free slot found
	if(key==OAPI_KEY_C&&KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{
		if(hUcgo.ReleaseOneCargo(iSelectedCargo)!=FALSE)
		{
			strcpy(SendCargHudMessage(),"Cargo release");
		}
		else
		{
			strcpy(SendCargHudMessage(),"Slot empty");
		}
		return 1;
	}
	// "8" show some info on cargo
	if(key==OAPI_KEY_8&&!KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{
		sprintf(SendCargHudMessage(),"Curent Payload mass %.0fkg in "
			"%i slots",hUcgo.GetCargoTotalMass(),hUcgo.GetNbrCargoLoaded());
	return 1;
	}

		// "7" Check O2 state
	if(key==OAPI_KEY_7&&!KEYMOD_SHIFT(kstate)&&!KEYMOD_CONTROL (kstate))
	{
		sprintf(SendCargHudMessage(),"Main Oxygen tank %.0fkg/1000kg. "
			,O2Check());
	return 1;
	}

	return 0;
}

//=========================================================
// UMmuCrewAddCallback & AddUMmuToVessel
// Again, a Dansteph creation, so I dont know a great deal about it, but its used in adding crew directly to the ship without entering through the main
// hatch. Fairly usefull, Id say.
//=========================================================

bool UMmuCrewAddCallback(void *id, char *str, void *data)
{
	if(strlen(str)<2||strlen(str)>38)
		return false;
	char *cPtr=(char*)data;	if(*cPtr==2){*cPtr=3;strcpy(cPtr+2,str);}
	else if(*cPtr==4){*cPtr=5;strcpy(cPtr+42,str);}
	else if(*cPtr==6){*cPtr=7;strcpy(cPtr+82,str);}return true;
}

void ShuttleD::AddUMmuToVessel(BOOL bStartAdding)
{
	if(bStartAdding==FALSE&&cAddUMmuToVessel[0]==0)
		return;
	if(bStartAdding==TRUE){
		int salut=sizeof(cAddUMmuToVessel);
		memset(cAddUMmuToVessel,0,sizeof(cAddUMmuToVessel));
		cAddUMmuToVessel[0]=1;
	}
	else if(cAddUMmuToVessel[0]==1){
		cAddUMmuToVessel[0]=2;
		oapiOpenInputBox ("Enter new crew member name (or hit escape to cancel)",UMmuCrewAddCallback,0,30,(void*)cAddUMmuToVessel);
	}
	else if(cAddUMmuToVessel[0]==3){
		cAddUMmuToVessel[0]=4;
		oapiOpenInputBox ("Enter age",UMmuCrewAddCallback,0,30,(void*)cAddUMmuToVessel);
	}
	else if(cAddUMmuToVessel[0]==5){
		cAddUMmuToVessel[0]=6;
		oapiOpenInputBox ("Enter Crew ID - Capt,Sec,Vip,Sci,Doc,Tech,Crew,Pax)",UMmuCrewAddCallback,0,30,(void*)cAddUMmuToVessel);
	}
	else if(cAddUMmuToVessel[0]==7){
		cAddUMmuToVessel[0]=0;
		int Age=max(5,min(100,atoi(&cAddUMmuToVessel[42])));
		if(Crew.AddCrewMember(&cAddUMmuToVessel[2],Age,70,70,&cAddUMmuToVessel[82])==TRUE){
			sprintf(SendHudMessage(),"\"%s\" aged %i added to vessel",&cAddUMmuToVessel[2],Age);
		}
		else{
			strcpy(SendHudMessage(),"Unable to add crew");
		}
	}
}



//=========================================================
// Load and Delete Module Stuff
// Apparenty used to load & delete special classes in the module. If theyre not deleted properly in ExitModule, they cause what I believe is called a
// memory leak, not good. So always remember to clean up after yourself!
//=========================================================
DLLCLBK void InitModule (HINSTANCE hModule)
{
	g_hDLL = hModule;
	hFont = CreateFont (-20, 3, 0, 0, 150, 0, 0, 0, 0, 0, 0, 0, 0, "Haettenschweiler");
	hPen = CreatePen (PS_SOLID, 3, RGB (120,220,120));
	hBrush = CreateSolidBrush (RGB(0,128,0));

	// perform global module initialisation here
}
DLLCLBK void ExitModule (HINSTANCE hModule)
{
	// perform module cleanup here
	DeleteObject (hFont);
	DeleteObject (hPen);
	DeleteObject (hBrush);

}

//=========================================================
// Load and Delete Vessel Stuff
// Appears similar to the part above, only I think its involved when creating or deleting vessels via the scenario editor.
//=========================================================
DLLCLBK VESSEL *ovcInit (OBJHANDLE hvessel, int flightmodel)
{
	return new ShuttleD (hvessel, flightmodel);
}

DLLCLBK void ovcExit (VESSEL *vessel)
{
	if (vessel)
		delete (ShuttleD*)vessel;
}

//=========================================================
// clbkPostCreation
// This is usually Orbitersound territory. Im not terribly familiar with how it works in 3.5 or 4.0, so I wont say much here. Better just to look
// up Dantephs tutoials in the Orbitersound docs. He can explain a lot of things better than me ;)
//=========================================================

void ShuttleD::clbkPostCreation (void)
{

	////////////////////////////////////////////////////////////////
	// 3-ORBITERSOUND EXAMPLE - INIT AND LOADING OF WAV
	// THIS MUST BE CALLED ABSOLUTELY IN THE "POSTCREATION CALLBACK" 
	////////////////////////////////////////////////////////////////
	// here we connect to OrbiterSound and store the returned ID in your class
	// this is the first thing to do. You must call this in "clbkPostCreation" 
	// (new version of ovcPostCreation wich is now obsolet)
	SHD=ConnectToOrbiterSoundDLL(GetHandle());

	SetMyDefaultWaveDirectory("Sound\\_CustomVesselsSounds\\Shuttle_D\\");

	RequestLoadVesselWave(SHD,GEARUP,"gearup.wav",INTERNAL_ONLY);
	RequestLoadVesselWave(SHD,GEARDOWN,"geardown.wav",INTERNAL_ONLY);
	RequestLoadVesselWave(SHD,PLBAYAOPEN,"plbayaopen.wav",INTERNAL_ONLY);
	RequestLoadVesselWave(SHD,PLBAYACLOSE,"plbayaclose.wav",INTERNAL_ONLY);
	RequestLoadVesselWave(SHD,PLBAYBOPEN,"plbaybopen.wav",INTERNAL_ONLY);
	RequestLoadVesselWave(SHD,PLBAYBCLOSE,"plbaybclose.wav",INTERNAL_ONLY);

	// now we are allowed for example to replace variable sound of OrbiterSound.
	// it will use them instead of the stock one when our vessel have the focus, see header file for parameter 
	// (you can replace more than the four below see parameters for "ReplaceStockSound3()")
	ReplaceStockSound(SHD,"mainext.wav",	REPLACE_MAIN_THRUST);
	ReplaceStockSound(SHD,"attfire.wav",		REPLACE_RCS_THRUST_ATTACK);
	ReplaceStockSound(SHD,"attsustain.wav",	REPLACE_RCS_THRUST_SUSTAIN);
	ReplaceStockSound(SHD,"aircond.wav",		REPLACE_AIR_CONDITIONNING);

	ReplaceStockSound(SHD,"VCamb1.wav",		REPLACE_COCKPIT_AMBIENCE_1);
	ReplaceStockSound(SHD,"VCamb2.wav",		REPLACE_COCKPIT_AMBIENCE_2);
	ReplaceStockSound(SHD,"VCamb3.wav",		REPLACE_COCKPIT_AMBIENCE_3);
	ReplaceStockSound(SHD,"VCamb4.wav",		REPLACE_COCKPIT_AMBIENCE_4);
	ReplaceStockSound(SHD,"VCamb5.wav",		REPLACE_COCKPIT_AMBIENCE_5);
	ReplaceStockSound(SHD,"VCamb6.wav",		REPLACE_COCKPIT_AMBIENCE_6);
	ReplaceStockSound(SHD,"VCamb7.wav",		REPLACE_COCKPIT_AMBIENCE_7);
	ReplaceStockSound(SHD,"aircond.wav",		REPLACE_COCKPIT_AMBIENCE_8);
	ReplaceStockSound(SHD,"aircond.wav",		REPLACE_COCKPIT_AMBIENCE_9);

	SoundOptionOnOff(SHD,PLAYRADIOATC,FALSE);
}

//=========================================================
// ~ShuttleD
// This is what I believe is called the destructor. A better name for it might be the Terminator, because Ill be back!!!
//=========================================================

ShuttleD::~ShuttleD() 
{
     delete th_rcs, th_main;
}

//=========================================================
// Hope the work I did commenting this source helped, please let me know what you think via email at johnnybgoode@rogers.com or on the Orbiter Forums
// at BruceJohnJennerLawso (or even my Shuttle-D development thread). This project has been about 10 months or so in the making & I
// hope you enjoy flying the Shuttle-D, as much as I enjoyed creating it. Stay tuned for 1.1, and
// 
// Hail the Probe!
//
//=========================================================
