#pragma once
#include "Orbitersdk.h"
#include "UMmuSDK.h"
#include "UCGOCargoSDK.h" //UCGO 2.0 copy past this line

// Vessel Parameters
// This is where data about the vessel class can be specified, then loaded latr under a shorter name. For example,
// const double EXP_EMPTYMASS = 14770; allows me to put EXP_EMPTYMASS in place of 14770 when I specify the empty 
// mass of the vehicle later.

const double EXP_SIZE = 31.4; // mean radius in meters
const VECTOR3 EXP_CS = {398.95,221.22,32.49}; //Shuttle-D cross section in m^2
const VECTOR3 EXP_PMI = {142.96,138.51,5.30}; //Principal Moments of Inertia, normalized, m^2
const double EXP_EMPTYMASS = 14770; //empty vessel mass in kg
const double EXP_FUELMASS =  11900; //max fuel mass in kg
const double EXP_RCS1FUELMASS =  120; //max fuel mass in kg
const double EXP_RCS2FUELMASS =  120; //max fuel mass in kg
const double EXP_RCS3FUELMASS =  120; //max fuel mass in kg
const double EXP_RCS4FUELMASS =  120; //max fuel mass in kg
const double EXP_RCS5FUELMASS =  120; //max fuel mass in kg
const double EXP_RCS6FUELMASS =  120; //max fuel mass in kg
const double EXP_RCS7FUELMASS =  120; //max fuel mass in kg
const double EXP_RCS8FUELMASS =  120; //max fuel mass in kg
const double EXP_RCS9FUELMASS =  120; //max fuel mass in kg
const double EXP_RCS10FUELMASS =  120; //max fuel mass in kg
const double EXP_RCS11FUELMASS =  120; //max fuel mass in kg
const double EXP_RCS12FUELMASS =  120; //max fuel mass in kg
const double VACSHD_ISP = 9000; //fuel-specific impulse in m/s
const double NMLSHD_ISP = 4200; //fuel-specific impulse in m/s
const double VACRCS_ISP = 1600; //fuel-specific impulse in m/s
const double NMLRCS_ISP = 710; //fuel-specific impulse in m/s
const double P_NML = 101.4e3;
const double EXP_MAXMAINTH = 87000; 
const double RCSTH0 = 1200; 
const double RCSTH1 = 1200;
const double RCSTH2 = 1470; 
const double RCSTH3 = 1470;
const double RCSTH4 = 1200; 
const double RCSTH5 = 1200;
const double RCSTH6 = 1200; 
const double RCSTH7 = 1200;
const double RCSTH8 = 1200; 
const double RCSTH9 = 1200;
const double RCSTH10 = 1200; 
const double RCSTH11 = 1200;
const double RCSTH12 = 1200; 
const double RCSTH13 = 1200;
const double RCSTH14 = 1200; 
const double RCSTH15 = 1200;
const double GEAR_OPERATING_SPEED = 0.10;
const double PLBAYA_OPERATING_SPEED = 0.07;
const double PLBAYB_OPERATING_SPEED = 0.18;


class ShuttleD :public VESSEL3
{
public:
    ShuttleD (OBJHANDLE hObj, int fmodel);



	~ShuttleD();

	// In this section functions to be called in the main body of the code are specified for use later. If a function placed in here is
	// never called later a "UNRESOLVED external" error will most likely pop up at compile-time. If a function is placed in the CPP but
	// not "created" here, it simply wont work.

	MESHHANDLE DVCInterior;
	void DefineAnimations();
	void clbkSetClassCaps (FILEHANDLE cfg);
	void clbkLoadStateEx (FILEHANDLE scn, void *status);
	bool clbkDrawHUD (int mode, const HUDPAINTSPEC *hps, oapi::Sketchpad *skp);
	void clbkSaveState (FILEHANDLE scn);
	void Timestep (double simt);
	void clbkPostStep (double simtt, double simdt, double mjd);
	void RevertGEAR (void);
	void RevertPLBAYA (void);
	void RevertPLBAYB (void);
	double UpdateMass ();
	double O2Check ();
	double Geardown ();
	double Gearup ();
	double MainBayOpen ();
	double MainBayClose ();
	double AuxBayOpen ();
	double AuxBayClose ();
	void clbkPostCreation(void);

	// Bits of code used to give the gear & payload bay references to work with instead of 0,0.1,0.2...
	// The different VC camera positions are also identified here as well.

	enum GEARStatus { GEAR_UP, GEAR_DOWN, GEAR_RAISING, GEAR_LOWERING } GEAR_status;
	enum PLBAYAStatus { PLBAYA_UP, PLBAYA_DOWN, PLBAYA_CLOSING, PLBAYA_OPENING } PLBAYA_status;
	enum PLBAYBStatus { PLBAYB_UP, PLBAYB_DOWN, PLBAYB_CLOSING, PLBAYB_OPENING } PLBAYB_status;
	enum {CAM_VCPILOT, CAM_VCPSNGR1, CAM_VCPSNGR2, CAM_VCPSNGR3, CAM_VCPSNGR4} campos;

	// This is a unique id, used to identify the ship in OrbiterSound.

	int SHD;	

	int  clbkConsumeBufferedKey (DWORD key, bool down, char *kstate);
	void clbkVisualCreated (VISHANDLE vis, int refcount);
	void clbkMFDMode (int mfd, int mode);
	bool clbkLoadVC (int id);
	bool clbkVCRedrawEvent (int id, int event, SURFHANDLE surf);
	bool clbkVCMouseEvent (int id, int event, VECTOR3 &p);
	VCMFDSPEC mfds_left;
	VCMFDSPEC mfds_right;

	//UMMU 2.0 Code
	//This section contains code which is used to add support for UMMU crew, created by Dansteph.

		// UMMU 2.0 DECLARATION
	UMMUCREWMANAGMENT Crew;
	int SelectedUmmuMember;				// for the SDK demo, select the member to eva
	int iActionAreaDemoStep;			// this is just to show one feature of action area.
	void clbkSetClassCaps_UMMu(void);	// our special SetClassCap function just added for more readability

	// The HUD display method variable, see PDF doc
	char cUmmuHudDisplay[255];			// UMmu hud char variable
	double dHudMessageDelay;			// UMmu hud display delay
	char *SendHudMessage(void);			// UMmu hud display function

	// "Allow user to add crew to your ship 
	// without scenery editor"
	char cAddUMmuToVessel[255];
	void AddUMmuToVessel(BOOL bStartAdding=FALSE);

	// This section is for UCGO, another terrific development library by Dansteph which allows developers to add cargo carrying capabilities. 

	// UCGO 2.0 CLASS HANDLE FUNCTION AND VARIABLES
	UCGO	hUcgo;						// Cargo class handle
	char   *SendCargHudMessage(void);	// Cargo hud display function
	char	cCargoHudDisplay[255];		// Cargo hud display char variable
	double	dCargHudMessageDelay;		// Cargo hud display delay
	int		iSelectedCargo;				// for the selection of cargos -1 by default

private:
	int iActiveDockNumber;
	UINT anim_gear;
	UINT anim_PLBAYA;
	UINT anim_PLBAYB;

	// Vessel specific parameters are called here like the # of kilos of LOX in the onboard tanks, the positions of the gear & payload bay doors,
	//	a variable that Im hoping to use as a randomizer for this project in the future. Variables are used to store & keep track of various pieces
	// of information during a simulation session, but need to be saved & loaded properly in clbkLoadStateEx & clbkSaveState
	// if they are to be persistent. Oh hi Face... ;)

	double GEAR_proc,PLBAYA_proc,PLBAYB_proc;
	double O2Tank;
	double MSStime;
	double Randomizer;
	PROPELLANT_HANDLE *MainFuel, *RCSRES, *RCS1, *RCS2, *RCS3, *RCS4, *RCS5, *RCS6, *RCS7, *RCS8, *RCS9, *RCS10, *RCS11, *RCS12;
	THRUSTER_HANDLE *th_rcs, *th_main;
};


HINSTANCE hDLL;
HFONT hFont;
HPEN hPen;
HBRUSH hBrush; 

#define AID_MFD1_LBUTTONS		0
#define AID_MFD1_RBUTTONS		1
#define MFD1_LBUTTON1			2
#define MFD1_LBUTTON2			3
#define MFD1_LBUTTON3			4
#define MFD1_LBUTTON4			5
#define MFD1_LBUTTON5			6
#define MFD1_LBUTTON6			7
#define MFD1_RBUTTON1			8
#define MFD1_RBUTTON2			9
#define MFD1_RBUTTON3			10
#define MFD1_RBUTTON4			11
#define MFD1_RBUTTON5			12
#define MFD1_RBUTTON6			13
#define MFD1_BBUTTON1			14
#define MFD1_BBUTTON2			15
#define MFD1_BBUTTON3			16
#define AID_MFD2_LBUTTONS		17
#define AID_MFD2_RBUTTONS		18
#define MFD2_LBUTTON1			19
#define MFD2_LBUTTON2			20
#define MFD2_LBUTTON3			21
#define MFD2_LBUTTON4			22
#define MFD2_LBUTTON5			23
#define MFD2_LBUTTON6			24
#define MFD2_RBUTTON1			25
#define MFD2_RBUTTON2			26
#define MFD2_RBUTTON3			27
#define MFD2_RBUTTON4			28
#define MFD2_RBUTTON5			29
#define MFD2_RBUTTON6			30
#define MFD2_BBUTTON1			31
#define MFD2_BBUTTON2			32
#define MFD2_BBUTTON3			33
#define AID_GEARDOWNSWITCH		34
#define AID_GEARUPSWITCH		35
#define AID_PLBAYAOPENSWITCH	36
#define AID_PLBAYACLOSESWITCH	37
#define AID_PLBAYBOPENSWITCH	38
#define AID_PLBAYBCLOSESWITCH	39
