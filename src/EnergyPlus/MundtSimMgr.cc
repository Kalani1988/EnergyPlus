// ObjexxFCL Headers
#include <ObjexxFCL/FArray.functions.hh>
#include <ObjexxFCL/Fmath.hh>

// EnergyPlus Headers
#include <MundtSimMgr.hh>
#include <DataEnvironment.hh>
#include <DataHeatBalance.hh>
#include <DataHeatBalFanSys.hh>
#include <DataHeatBalSurface.hh>
#include <DataLoopNode.hh>
#include <DataPrecisionGlobals.hh>
#include <DataRoomAirModel.hh>
#include <DataSurfaces.hh>
#include <DataZoneEquipment.hh>
#include <InputProcessor.hh>
#include <InternalHeatGains.hh>
#include <OutputProcessor.hh>
#include <Psychrometrics.hh>
#include <UtilityRoutines.hh>

namespace EnergyPlus {

namespace MundtSimMgr {

	// MODULE INFORMATION:
	//       AUTHOR         Brent Griffith
	//       DATE WRITTEN   February 2002
	//       RE-ENGINEERED  June 2003, EnergyPlus Implementation (CC)
	//       MODIFIED       February 2004, fix allocate-deallocate problem (CC)

	// PURPOSE OF THIS MODULE:
	// This module is the main module for running the
	// nodal air Mundt model...

	// METHODOLOGY EMPLOYED:
	// This module contains all subroutines required by the mundt model.
	// The following modules from AirToolkit included in this module are:
	// 1) MundtSimMgr Module,
	// 2) MundtInputMgr Module, and
	// 3) DataMundt Module,

	// REFERENCES:
	// AirToolkit source code

	// OTHER NOTES:
	// na

	// Using/Aliasing
	using namespace DataPrecisionGlobals;
	using InputProcessor::SameString;

	// Data
	// MODULE PARAMETER DEFINITIONS:
	Real64 const CpAir( 1005.0 ); // Specific heat of air
	Real64 const MinSlope( 0.001 ); // Bound on result from Mundt model
	Real64 const MaxSlope( 5.0 ); // Bound on result from Mundt Model

	static std::string const BlankString;

	// MODULE DERIVED TYPE DEFINITIONS:

	// INTERFACE BLOCK SPECIFICATIONS:
	// na

	// MODULE VARIABLE DECLARATIONS:
	FArray1D_int FloorSurfSetIDs; // fixed variable for floors
	FArray1D_int TheseSurfIDs; // temporary working variable
	int MundtCeilAirID( 0 ); // air node index in AirDataManager
	int MundtFootAirID( 0 ); // air node index in AirDataManager
	int SupplyNodeID( 0 ); // air node index in AirDataManager
	int TstatNodeID( 0 ); // air node index in AirDataManager
	int ReturnNodeID( 0 ); // air node index in AirDataManager
	int NumRoomNodes( 0 ); // number of nodes connected to walls
	int NumFloorSurfs( 0 ); // total number of surfaces for floor
	FArray1D_int RoomNodeIDs; // ids of the first NumRoomNode Air Nodes
	FArray1D_int ID1dSurf; // numbers used to identify surfaces
	int MundtZoneNum( 0 ); // index of zones using Mundt model
	Real64 ZoneHeight( 0.0 ); // zone height
	Real64 ZoneFloorArea( 0.0 ); // zone floor area
	Real64 QventCool( 0.0 ); // heat gain due to ventilation
	Real64 ConvIntGain( 0.0 ); // heat gain due to internal gains
	Real64 SupplyAirTemp( 0.0 ); // supply air temperature
	Real64 SupplyAirVolumeRate( 0.0 ); // supply air volume flowrate
	Real64 ZoneAirDensity( 0.0 ); // zone air density
	Real64 QsysCoolTot( 0.0 ); // zone sensible cooling load

	// SUBROUTINE SPECIFICATIONS FOR MODULE MundtSimMgr

	// main subsroutine

	// Routines for transferring data between surface and air domains

	// Routines for actual calculations in Mundt model

	// Object Data
	FArray1D< DefineZoneData > ZoneData; // zone data
	FArray2D< DefineLinearModelNode > LineNode; // air nodes
	FArray2D< DefineSurfaceSettings > MundtAirSurf; // surfaces
	FArray1D< DefineSurfaceSettings > FloorSurf; // floor

	// MODULE SUBROUTINES:

	// Functions

	void
	ManageMundtModel( int const ZoneNum ) // index number for the specified zone
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Chanvit Chantrasrisalai
		//       DATE WRITTEN   July 2003
		//       MODIFIED       February 2004, fix allocate-deallocate problem (CC)
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		//   manage the Mundt model

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		// na
		static bool FirstTimeFlag( true ); // Used for allocating arrays
		bool ErrorsFound;

		// FLOW:

		// initialize Mundt model data
		if ( FirstTimeFlag ) {
			InitMundtModel();
			FirstTimeFlag = false;
		}

		// identify the current zone index for zones using Mundt model
		MundtZoneNum = ZoneData( ZoneNum ).MundtZoneIndex;

		// transfer data from surface domain to air domain for the specified zone
		GetSurfHBDataForMundtModel( ZoneNum );

		// use the Mundt model only for cooling case
		if ( ( SupplyAirVolumeRate > 0.0001 ) && ( QsysCoolTot > 0.0001 ) ) {

			// setup Mundt model
			ErrorsFound = false;
			SetupMundtModel( ZoneNum, ErrorsFound );
			if ( ErrorsFound ) ShowFatalError( "ManageMundtModel: Errors in setting up Mundt Model. Preceding condition(s) cause termination." );

			// perform Mundt model calculations
			CalcMundtModel( ZoneNum );

		}

		// transfer data from air domain back to surface domain for the specified zone
		SetSurfHBDataForMundtModel( ZoneNum );

	}

	//*****************************************************************************************

	void
	InitMundtModel()
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Chanvit Chantrasrisalai
		//       DATE WRITTEN   February 2004
		//       MODIFIED       na
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		//     initialize Mundt-model variables

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:

		// Using/Aliasing
		using DataGlobals::NumOfZones;
		using DataRoomAirModel::TotNumOfAirNodes;
		using DataRoomAirModel::TotNumOfZoneAirNodes;
		using DataRoomAirModel::AirModel;
		using DataRoomAirModel::AirNode;
		using DataRoomAirModel::RoomAirModel_Mundt;
		using DataRoomAirModel::MundtRoomAirNode;
		using DataRoomAirModel::FloorAirNode;
		using DataSurfaces::Surface;
		using DataHeatBalance::Zone;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:
		// na

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int SurfNum; // index for surfaces
		int SurfFirst; // index number for the first surface in the specified zone
		int NumOfSurfs; // number of the first surface in the specified zone
		int NodeNum; // index for air nodes
		int ZoneIndex; // index for zones
		int NumOfAirNodes; // total number of nodes in each zone
		int NumOfMundtZones; // number of zones using the Mundt model
		int MundtZoneIndex; // index for zones using the Mundt model
		int MaxNumOfSurfs; // maximum of number of surfaces
		int MaxNumOfFloorSurfs; // maximum of number of surfaces
		int MaxNumOfAirNodes; // maximum of number of air nodes
		int MaxNumOfRoomNodes; // maximum of number of nodes connected to walls
		int RoomNodesCount; // number of nodes connected to walls
		int FloorSurfCount; // number of nodes connected to walls
		int AirNodeBeginNum; // index number of the first air node for this zone
		int AirNodeNum; // index for air nodes
		bool AirNodeFoundFlag; // flag used for error check
		bool ErrorsFound; // true if errors found in init

		// FLOW:

		// allocate and initialize zone data
		ZoneData.allocate( NumOfZones );
		ZoneData.SurfFirst() = 0;
		ZoneData.NumOfSurfs() = 0;
		ZoneData.MundtZoneIndex() = 0;

		// get zone data
		NumOfMundtZones = 0;
		MaxNumOfSurfs = 0;
		MaxNumOfFloorSurfs = 0;
		MaxNumOfAirNodes = 0;
		MaxNumOfRoomNodes = 0;
		ErrorsFound = false;
		for ( ZoneIndex = 1; ZoneIndex <= NumOfZones; ++ZoneIndex ) {
			if ( AirModel( ZoneIndex ).AirModelType == RoomAirModel_Mundt ) {
				// find number of zones using the Mundt model
				++NumOfMundtZones;
				// find maximum number of surfaces in zones using the Mundt model
				SurfFirst = Zone( ZoneIndex ).SurfaceFirst;
				NumOfSurfs = Zone( ZoneIndex ).SurfaceLast - SurfFirst + 1;
				MaxNumOfSurfs = max( MaxNumOfSurfs, NumOfSurfs );
				// fine maximum number of air nodes in zones using the Mundt model
				NumOfAirNodes = TotNumOfZoneAirNodes( ZoneIndex );
				MaxNumOfAirNodes = max( MaxNumOfAirNodes, NumOfAirNodes );
				// assign zone data
				ZoneData( ZoneIndex ).SurfFirst = SurfFirst;
				ZoneData( ZoneIndex ).NumOfSurfs = NumOfSurfs;
				ZoneData( ZoneIndex ).MundtZoneIndex = NumOfMundtZones;
			}
		}

		// allocate and initialize surface and air-node data
		ID1dSurf.allocate( MaxNumOfSurfs );
		TheseSurfIDs.allocate( MaxNumOfSurfs );
		MundtAirSurf.allocate( NumOfMundtZones, MaxNumOfSurfs );
		LineNode.allocate( NumOfMundtZones, MaxNumOfAirNodes );
		for ( SurfNum = 1; SurfNum <= MaxNumOfSurfs; ++SurfNum ) ID1dSurf( SurfNum ) = SurfNum;
		MundtAirSurf.Area() = 0.0;
		MundtAirSurf.Temp() = 25.0;
		MundtAirSurf.Hc() = 0.0;
		MundtAirSurf.TMeanAir() = 25.0;
		LineNode.AirNodeName() = "";
		LineNode.ClassType() = -1;
		LineNode.Height() = 0.0;
		LineNode.Temp() = 25.0;

		// get constant data (unchanged over time) for surfaces and air nodes
		for ( MundtZoneIndex = 1; MundtZoneIndex <= NumOfMundtZones; ++MundtZoneIndex ) {

			Zone_Loop: for ( ZoneIndex = 1; ZoneIndex <= NumOfZones; ++ZoneIndex ) {

				if ( ZoneData( ZoneIndex ).MundtZoneIndex == MundtZoneIndex ) {
					// get surface data
					for ( SurfNum = 1; SurfNum <= ZoneData( ZoneIndex ).NumOfSurfs; ++SurfNum ) {
						MundtAirSurf( MundtZoneIndex, SurfNum ).Area = Surface( ZoneData( ZoneIndex ).SurfFirst + SurfNum - 1 ).Area;
					}

					// get air node data
					RoomNodesCount = 0;
					FloorSurfCount = 0;
					for ( NodeNum = 1; NodeNum <= TotNumOfZoneAirNodes( ZoneIndex ); ++NodeNum ) {

						LineNode( MundtZoneIndex, NodeNum ).SurfMask.allocate( ZoneData( ZoneIndex ).NumOfSurfs );

						if ( NodeNum == 1 ) {
							AirNodeBeginNum = NodeNum;
						}

						// error check for debugging
						if ( AirNodeBeginNum > TotNumOfAirNodes ) {
							ShowFatalError( "An array bound exceeded. Error in InitMundtModel subroutine of MundtSimMgr." );
						}

						AirNodeFoundFlag = false;
						for ( AirNodeNum = AirNodeBeginNum; AirNodeNum <= TotNumOfAirNodes; ++AirNodeNum ) {
							if ( SameString( AirNode( AirNodeNum ).ZoneName, Zone( ZoneIndex ).Name ) ) {
								LineNode( MundtZoneIndex, NodeNum ).ClassType = AirNode( AirNodeNum ).ClassType;
								LineNode( MundtZoneIndex, NodeNum ).AirNodeName = AirNode( AirNodeNum ).Name;
								LineNode( MundtZoneIndex, NodeNum ).Height = AirNode( AirNodeNum ).Height;
								LineNode( MundtZoneIndex, NodeNum ).SurfMask = AirNode( AirNodeNum ).SurfMask;
								SetupOutputVariable( "Room Air Node Air Temperature [C]", LineNode( MundtZoneIndex, NodeNum ).Temp, "HVAC", "Average", LineNode( MundtZoneIndex, NodeNum ).AirNodeName );

								AirNodeBeginNum = AirNodeNum + 1;
								AirNodeFoundFlag = true;

								break;
							}
						}

						// error check for debugging
						if ( ! AirNodeFoundFlag ) {
							ShowSevereError( "InitMundtModel: Air Node in Zone=\"" + Zone( ZoneIndex ).Name + "\" is not found." );
							ErrorsFound = true;
							continue;
						}

						// count air nodes connected to walls in each zone
						if ( LineNode( MundtZoneIndex, NodeNum ).ClassType == MundtRoomAirNode ) {
							++RoomNodesCount;
						}

						// count floors in each zone
						if ( LineNode( MundtZoneIndex, NodeNum ).ClassType == FloorAirNode ) {
							FloorSurfCount += count( LineNode( MundtZoneIndex, NodeNum ).SurfMask );
						}

					}
					// got data for this zone so exit the zone loop
					if ( AirNodeFoundFlag ) {
						goto Zone_Loop_exit;
					}

				}

				Zone_Loop_loop: ;
			}
			Zone_Loop_exit: ;

			MaxNumOfRoomNodes = max( MaxNumOfRoomNodes, RoomNodesCount );
			MaxNumOfFloorSurfs = max( MaxNumOfFloorSurfs, FloorSurfCount );

		}

		if ( ErrorsFound ) ShowFatalError( "InitMundtModel: Preceding condition(s) cause termination." );

		// allocate arrays
		RoomNodeIDs.allocate( MaxNumOfRoomNodes );
		FloorSurfSetIDs.allocate( MaxNumOfFloorSurfs );
		FloorSurf.allocate( MaxNumOfFloorSurfs );

	}

	//*****************************************************************************************

	void
	GetSurfHBDataForMundtModel( int const ZoneNum ) // index number for the specified zone
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Weixiu Kong
		//       DATE WRITTEN   April 2003
		//       MODIFIED       July 2003 (CC)
		//                      February 2004, fix allocate-deallocate problem (CC)
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		//     map data from surface domain to air domain for each particular zone

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using DataGlobals::NumOfZones;
		using DataLoopNode::Node;
		using DataEnvironment::OutBaroPress;
		using DataHeatBalFanSys::ZoneAirHumRat;
		using DataHeatBalFanSys::MCPI;
		using DataHeatBalFanSys::MAT;
		using DataHeatBalFanSys::SumConvHTRadSys;
		using DataHeatBalFanSys::SumConvPool;
		using DataHeatBalFanSys::SysDepZoneLoadsLagged;
		using DataHeatBalFanSys::NonAirSystemResponse;
		using DataHeatBalSurface::TempSurfIn;
		using DataSurfaces::Surface;
		using DataHeatBalance::Zone;
		using DataHeatBalance::HConvIn;
		using DataHeatBalance::ZoneIntGain;
		using DataHeatBalance::RefrigCaseCredit;
		using DataZoneEquipment::ZoneEquipConfig;
		using Psychrometrics::PsyWFnTdpPb;
		using Psychrometrics::PsyCpAirFnWTdb;
		using Psychrometrics::PsyRhoAirFnPbTdbW;
		using InternalHeatGains::SumAllInternalConvectionGains;
		using InternalHeatGains::SumAllReturnAirConvectionGains;

		// Locals
		Real64 CpAir; // specific heat

		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int SurfNum; // index for surfaces
		int NodeNum; // index for air nodes
		Real64 SumSysMCp; // zone sum of air system MassFlowRate*Cp
		Real64 SumSysMCpT; // zone sum of air system MassFlowRate*Cp*T
		Real64 MassFlowRate; // mass flowrate
		Real64 NodeTemp; // node temperature
		int ZoneNode; // index number for specified zone node
		Real64 ZoneMassFlowRate; // zone mass flowrate
		int ZoneEquipConfigNum; // index number for zone equipment configuration
		Real64 ZoneMult; // total zone multiplier
		Real64 RetAirConvGain;

		// FLOW:

		// determine ZoneEquipConfigNum for this zone
		ZoneEquipConfigNum = ZoneNum;
		// check whether this zone is a controlled zone or not
		if ( ! Zone( ZoneNum ).IsControlled ) {
			ShowFatalError( "Zones must be controlled for Mundt air model. No system serves zone " + Zone( ZoneNum ).Name );
			return;
		}

		// determine information required by Mundt model
		ZoneHeight = Zone( ZoneNum ).CeilingHeight;
		ZoneFloorArea = Zone( ZoneNum ).FloorArea;
		ZoneMult = Zone( ZoneNum ).Multiplier * Zone( ZoneNum ).ListMultiplier;

		// supply air flowrate is the same as zone air flowrate
		ZoneNode = Zone( ZoneNum ).SystemZoneNodeNumber;
		ZoneAirDensity = PsyRhoAirFnPbTdbW( OutBaroPress, MAT( ZoneNum ), PsyWFnTdpPb( MAT( ZoneNum ), OutBaroPress ) );
		ZoneMassFlowRate = Node( ZoneNode ).MassFlowRate;
		SupplyAirVolumeRate = ZoneMassFlowRate / ZoneAirDensity;
		if ( ZoneMassFlowRate <= 0.0001 ) {
			// system is off
			QsysCoolTot = 0.0;
		} else {
			// determine supply air conditions
			SumSysMCp = 0.0;
			SumSysMCpT = 0.0;
			for ( NodeNum = 1; NodeNum <= ZoneEquipConfig( ZoneEquipConfigNum ).NumInletNodes; ++NodeNum ) {
				NodeTemp = Node( ZoneEquipConfig( ZoneEquipConfigNum ).InletNode( NodeNum ) ).Temp;
				MassFlowRate = Node( ZoneEquipConfig( ZoneEquipConfigNum ).InletNode( NodeNum ) ).MassFlowRate;
				CpAir = PsyCpAirFnWTdb( ZoneAirHumRat( ZoneNum ), NodeTemp );
				SumSysMCp += MassFlowRate * CpAir;
				SumSysMCpT += MassFlowRate * CpAir * NodeTemp;
			}
			// prevent dividing by zero due to zero supply air flow rate
			if ( SumSysMCp <= 0.0 ) {
				SupplyAirTemp = Node( ZoneEquipConfig( ZoneEquipConfigNum ).InletNode( 1 ) ).Temp;
			} else {
				// a weighted average of the inlet temperatures
				SupplyAirTemp = SumSysMCpT / SumSysMCp;
			}
			// determine cooling load
			CpAir = PsyCpAirFnWTdb( ZoneAirHumRat( ZoneNum ), MAT( ZoneNum ) );
			QsysCoolTot = -( SumSysMCpT - ZoneMassFlowRate * CpAir * MAT( ZoneNum ) );
		}
		// determine heat gains
		SumAllInternalConvectionGains( ZoneNum, ConvIntGain );
		ConvIntGain += SumConvHTRadSys( ZoneNum ) + SumConvPool( ZoneNum ) + SysDepZoneLoadsLagged( ZoneNum ) + NonAirSystemResponse( ZoneNum ) / ZoneMult;

		// Add heat to return air if zonal system (no return air) or cycling system (return air frequently very
		// low or zero)
		if ( Zone( ZoneNum ).NoHeatToReturnAir ) {
			SumAllReturnAirConvectionGains( ZoneNum, RetAirConvGain );
			ConvIntGain += RetAirConvGain;
		}

		QventCool = -MCPI( ZoneNum ) * ( Zone( ZoneNum ).OutDryBulbTemp - MAT( ZoneNum ) );

		// get surface data
		for ( SurfNum = 1; SurfNum <= ZoneData( ZoneNum ).NumOfSurfs; ++SurfNum ) {
			MundtAirSurf( MundtZoneNum, SurfNum ).Temp = TempSurfIn( ZoneData( ZoneNum ).SurfFirst + SurfNum - 1 );
			MundtAirSurf( MundtZoneNum, SurfNum ).Hc = HConvIn( ZoneData( ZoneNum ).SurfFirst + SurfNum - 1 );
		}

	}

	//*****************************************************************************************

	void
	SetupMundtModel(
		int const ZoneNum, // index number for the specified zone
		bool & ErrorsFound // true if problems setting up model
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Brent Griffith
		//       DATE WRITTEN   Febraury 2002
		//       RE-ENGINEERED  June 2003, EnergyPlus Implementation (CC)
		//       MODIFIED       February 2004, fix allocate-deallocate problem (CC)

		// PURPOSE OF THIS SUBROUTINE:
		//   Subroutine must be called once before main model calculation
		//   need to pass some zone characteristics only once
		//   initializes module level variables, collect info from Air Data Manager

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// Using/Aliasing
		using namespace DataRoomAirModel;
		using DataHeatBalance::Zone;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int NodeNum; // index for air nodes
		int SurfNum; // index for surfaces

		// FLOW:

		// set up air node ID
		NumRoomNodes = 0;
		for ( NodeNum = 1; NodeNum <= TotNumOfZoneAirNodes( ZoneNum ); ++NodeNum ) {
			{ auto const SELECT_CASE_var( LineNode( MundtZoneNum, NodeNum ).ClassType );
			if ( SELECT_CASE_var == InletAirNode ) { //inlet
				SupplyNodeID = NodeNum;
			} else if ( SELECT_CASE_var == FloorAirNode ) { // floor
				MundtFootAirID = NodeNum;
			} else if ( SELECT_CASE_var == ControlAirNode ) { // thermostat
				TstatNodeID = NodeNum;
			} else if ( SELECT_CASE_var == CeilingAirNode ) { // ceiling
				MundtCeilAirID = NodeNum;
			} else if ( SELECT_CASE_var == MundtRoomAirNode ) { // wall
				++NumRoomNodes;
				RoomNodeIDs( NumRoomNodes ) = NodeNum;
			} else if ( SELECT_CASE_var == ReturnAirNode ) { // return
				ReturnNodeID = NodeNum;
			} else {
				ShowSevereError( "SetupMundtModel: Non-Standard Type of Air Node for Mundt Model" );
				ErrorsFound = true;
			}}
		}

		//  get number of floors in the zone and setup FloorSurfSetIDs
		if ( MundtFootAirID > 0 ) {
			NumFloorSurfs = count( LineNode( MundtZoneNum, MundtFootAirID ).SurfMask );
			FloorSurfSetIDs = pack( ID1dSurf, LineNode( MundtZoneNum, MundtFootAirID ).SurfMask );
			// initialize floor surface data (a must since NumFloorSurfs is varied among zones)
			FloorSurf.Temp() = 25.0;
			FloorSurf.Hc() = 0.0;
			FloorSurf.Area() = 0.0;
			// get floor surface data
			for ( SurfNum = 1; SurfNum <= NumFloorSurfs; ++SurfNum ) {
				FloorSurf( SurfNum ).Temp = MundtAirSurf( MundtZoneNum, FloorSurfSetIDs( SurfNum ) ).Temp;
				FloorSurf( SurfNum ).Hc = MundtAirSurf( MundtZoneNum, FloorSurfSetIDs( SurfNum ) ).Hc;
				FloorSurf( SurfNum ).Area = MundtAirSurf( MundtZoneNum, FloorSurfSetIDs( SurfNum ) ).Area;
			}
		} else {
			ShowSevereError( "SetupMundtModel: Mundt model has no FloorAirNode, Zone=" + Zone( ZoneNum ).Name );
			ErrorsFound = true;
		}

	}

	//*****************************************************************************************

	void
	CalcMundtModel( int const ZoneNum ) // index number for the specified zone
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Brent Griffith
		//       DATE WRITTEN   September 2001
		//       RE-ENGINEERED  July 2003, EnergyPlus Implementation (CC)
		//       MODIFIED       February 2004, fix allocate-deallocate problem (CC)

		// PURPOSE OF THIS SUBROUTINE:
		//   Compute the simplified version of Mundt and store results in Air data Manager
		//   argument passing is plentiful but are IN and nothing out.
		//   these variables are scaler conditions at current HB day,timestep, and iteration
		//   This subroutine is USE'ed by heat balance driver (top level module)

		// METHODOLOGY EMPLOYED:
		//   apply Mundt's simple model for delta Temp head-foot and update values in Air data manager.

		// REFERENCES:
		// na

		// Using/Aliasing
		using DataRoomAirModel::ConvectiveFloorSplit;
		using DataRoomAirModel::InfiltratFloorSplit;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		Real64 TAirFoot; // air temperature at the floor
		Real64 TAirCeil; // air temperature at the ceiling
		Real64 TLeaving; // air temperature leaving zone (= return air temp)
		Real64 TControlPoint; // air temperature at thermostat
		Real64 Slope; // vertical air temperature gradient (slope) from Mundt equations
		Real64 QequipConvFloor; // convective gain at the floor due to internal heat sources
		Real64 QSensInfilFloor; // convective gain at the floor due to infiltration
		Real64 FloorSumHAT; // sum of hci*area*temp at the floor
		Real64 FloorSumHA; // sum of hci*area at the floor
		Real64 TThisNode; // dummy variable for air node temp
		int NodeNum; // index for air nodes
		int SurfNum; // index for surfaces
		int SurfCounted; // number of surfaces assciated with an air node

		// FLOW:

		//   apply floor splits
		QequipConvFloor = ConvectiveFloorSplit( ZoneNum ) * ConvIntGain;
		QSensInfilFloor = -InfiltratFloorSplit( ZoneNum ) * QventCool;

		// Begin computations for Mundt model

		// do summations for floor surfaces of this zone
		FloorSumHAT = sum( FloorSurf.Area() * FloorSurf.Hc() * FloorSurf.Temp() );
		FloorSumHA = sum( FloorSurf.Area() * FloorSurf.Hc() );

		// Eq 2.2 in ASHRAE RP 1222 Final report
		TAirFoot = ( ( ZoneAirDensity * CpAir * SupplyAirVolumeRate * SupplyAirTemp ) + ( FloorSumHAT ) + QequipConvFloor + QSensInfilFloor ) / ( ( ZoneAirDensity * CpAir * SupplyAirVolumeRate ) + ( FloorSumHA ) );

		// prevent dividing by zero due to zero cooling load (or zero supply air flow rate)
		if ( QsysCoolTot <= 0.0 ) {
			TLeaving = SupplyAirTemp;
		} else {
			// Eq 2.3 in ASHRAE RP 1222 Final report
			TLeaving = ( QsysCoolTot / ( ZoneAirDensity * CpAir * SupplyAirVolumeRate ) ) + SupplyAirTemp;
		}

		// Eq 2.4 in ASHRAE RP 1222 Final report
		Slope = ( TLeaving - TAirFoot ) / ( LineNode( MundtZoneNum, ReturnNodeID ).Height - LineNode( MundtZoneNum, MundtFootAirID ).Height );
		// check slope
		if ( Slope > MaxSlope ) {
			Slope = MaxSlope;
			TAirFoot = TLeaving - ( Slope * ( LineNode( MundtZoneNum, ReturnNodeID ).Height - LineNode( MundtZoneNum, MundtFootAirID ).Height ) );
		}
		if ( Slope < MinSlope ) { // pretty much vertical
			Slope = MinSlope;
			TAirFoot = TLeaving;
		}

		// Eq 2.4 in ASHRAE RP 1222 Final report
		TAirCeil = TLeaving - ( Slope * ( LineNode( MundtZoneNum, ReturnNodeID ).Height - LineNode( MundtZoneNum, MundtCeilAirID ).Height ) );

		TControlPoint = TLeaving - ( Slope * ( LineNode( MundtZoneNum, ReturnNodeID ).Height - LineNode( MundtZoneNum, TstatNodeID ).Height ) );

		// determine air node temperatures in this zone
		SetNodeResult( SupplyNodeID, SupplyAirTemp );
		SetNodeResult( ReturnNodeID, TLeaving );
		SetNodeResult( MundtCeilAirID, TAirCeil );
		SetNodeResult( MundtFootAirID, TAirFoot );
		SetNodeResult( TstatNodeID, TControlPoint );

		for ( SurfNum = 1; SurfNum <= NumFloorSurfs; ++SurfNum ) {
			SetSurfTmeanAir( FloorSurfSetIDs( SurfNum ), TAirFoot );
		}

		SurfCounted = count( LineNode( MundtZoneNum, MundtCeilAirID ).SurfMask );
		TheseSurfIDs = pack( ID1dSurf, LineNode( MundtZoneNum, MundtCeilAirID ).SurfMask );
		for ( SurfNum = 1; SurfNum <= SurfCounted; ++SurfNum ) {
			SetSurfTmeanAir( TheseSurfIDs( SurfNum ), TAirCeil );
		}

		for ( NodeNum = 1; NodeNum <= NumRoomNodes; ++NodeNum ) {
			TThisNode = TLeaving - ( Slope * ( LineNode( MundtZoneNum, ReturnNodeID ).Height - LineNode( MundtZoneNum, RoomNodeIDs( NodeNum ) ).Height ) );
			SetNodeResult( RoomNodeIDs( NodeNum ), TThisNode );
			SurfCounted = count( LineNode( MundtZoneNum, RoomNodeIDs( NodeNum ) ).SurfMask );
			TheseSurfIDs = pack( ID1dSurf, LineNode( MundtZoneNum, RoomNodeIDs( NodeNum ) ).SurfMask );
			for ( SurfNum = 1; SurfNum <= SurfCounted; ++SurfNum ) {
				SetSurfTmeanAir( TheseSurfIDs( SurfNum ), TThisNode );
			}
		}

	}

	//*****************************************************************************************

	void
	SetNodeResult(
		int const NodeID, // node ID
		Real64 const TempResult // temperature for the specified air node
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Brent Griffith
		//       DATE WRITTEN   September 2002
		//       RE-ENGINEERED  April 2003, Weixiu Kong, EnergyPlus Implementation
		//       MODIFIED       February 2004, fix allocate-deallocate problem (CC)

		// PURPOSE OF THIS SUBROUTINE:
		//   provide set routine for reporting results
		//   to AirDataManager from air model

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		// na

		// FLOW:

		LineNode( MundtZoneNum, NodeID ).Temp = TempResult;

	}

	//*****************************************************************************************

	void
	SetSurfTmeanAir(
		int const SurfID, // surface ID
		Real64 const TeffAir // temperature of air node adjacent to the specified surface
	)
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Brent Griffith
		//       DATE WRITTEN   September 2002
		//       RE-ENGINEERED  April 2003, Wiexiu Kong, EnergyPlus Implementation
		//       MODIFIED       February 2004, fix allocate-deallocate problem (CC)

		// PURPOSE OF THIS SUBROUTINE:
		//   provide set routine for air model prediction of
		//   effective air for single surface

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:
		// na

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		// na

		// FLOW:

		MundtAirSurf( MundtZoneNum, SurfID ).TMeanAir = TeffAir;

	}

	//*****************************************************************************************

	void
	SetSurfHBDataForMundtModel( int const ZoneNum ) // index number for the specified zone
	{

		// SUBROUTINE INFORMATION:
		//       AUTHOR         Chanvit Chantrasrisalai
		//       DATE WRITTEN   July 2003
		//       MODIFIED       February 2004, fix allocate-deallocate problem (CC)
		//       RE-ENGINEERED  na

		// PURPOSE OF THIS SUBROUTINE:
		//     map data from air domain back to surface domain for each particular zone

		// METHODOLOGY EMPLOYED:
		// na

		// REFERENCES:
		// na

		// USE STATEMENTS:

		// Using/Aliasing
		using DataLoopNode::Node;
		using DataRoomAirModel::AirModel;
		using DataRoomAirModel::DirectCoupling;
		using DataSurfaces::Surface;
		using DataSurfaces::AdjacentAirTemp;
		using DataSurfaces::ZoneMeanAirTemp;
		using DataHeatBalance::Zone;
		using DataHeatBalance::TempEffBulkAir;
		using DataZoneEquipment::ZoneEquipConfig;
		using DataHeatBalFanSys::MAT;
		using DataHeatBalFanSys::ZT;
		using DataHeatBalFanSys::TempZoneThermostatSetPoint;
		using DataHeatBalFanSys::TempTstatAir;

		// Locals
		// SUBROUTINE ARGUMENT DEFINITIONS:

		// SUBROUTINE PARAMETER DEFINITIONS:
		// na

		// INTERFACE BLOCK SPECIFICATIONS:
		// na

		// DERIVED TYPE DEFINITIONS:
		// na

		// SUBROUTINE LOCAL VARIABLE DECLARATIONS:
		int SurfNum; // index for surfaces
		int SurfFirst; // index number of the first surface in the zone
		int NumOfSurfs; // number of surfaces in the zone
		int ZoneNodeNum; // index number of the zone node
		Real64 DeltaTemp; // dummy variable for temperature difference
		Real64 TRoomAverage; // dummy variable for mean air temperature
		// FLOW:

		// get surface info
		SurfFirst = ZoneData( ZoneNum ).SurfFirst;
		NumOfSurfs = ZoneData( ZoneNum ).NumOfSurfs;

		if ( ( SupplyAirVolumeRate > 0.0001 ) && ( QsysCoolTot > 0.0001 ) ) { // Controlled zone when the system is on

			if ( AirModel( ZoneNum ).TempCoupleScheme == DirectCoupling ) {
				// Use direct coupling scheme to report air temperatures back to surface/system domains
				// a) Bulk air temperatures -> TempEffBulkAir(SurfNum)
				for ( SurfNum = 1; SurfNum <= NumOfSurfs; ++SurfNum ) {
					TempEffBulkAir( SurfFirst + SurfNum - 1 ) = MundtAirSurf( MundtZoneNum, SurfNum ).TMeanAir;
					// set flag for reference air temperature
					Surface( SurfFirst + SurfNum - 1 ).TAirRef = AdjacentAirTemp;
				}
				// b) Average zone air temperature -> ZT(ZoneNum)
				// For Mundt model, average room air is the weighted value of floor and ceiling air temps
				TRoomAverage = ( LineNode( MundtZoneNum, MundtCeilAirID ).Temp + LineNode( MundtZoneNum, MundtFootAirID ).Temp ) / 2;
				//ZT(ZoneNum) = TRoomAverage
				// c) Leaving-zone air temperature -> Node(ZoneNode)%Temp
				ZoneNodeNum = Zone( ZoneNum ).SystemZoneNodeNumber;
				Node( ZoneNodeNum ).Temp = LineNode( MundtZoneNum, ReturnNodeID ).Temp;
				// d) Thermostat air temperature -> TempTstatAir(ZoneNum)
				TempTstatAir( ZoneNum ) = LineNode( MundtZoneNum, TstatNodeID ).Temp;
			} else {
				// Use indirect coupling scheme to report air temperatures back to surface/system domains
				// a) Bulk air temperatures -> TempEffBulkAir(SurfNum)
				for ( SurfNum = 1; SurfNum <= NumOfSurfs; ++SurfNum ) {
					DeltaTemp = MundtAirSurf( MundtZoneNum, SurfNum ).TMeanAir - LineNode( MundtZoneNum, TstatNodeID ).Temp;
					TempEffBulkAir( SurfFirst + SurfNum - 1 ) = TempZoneThermostatSetPoint( ZoneNum ) + DeltaTemp;
					// set flag for reference air temperature
					Surface( SurfFirst + SurfNum - 1 ).TAirRef = AdjacentAirTemp;
				}
				// b) Average zone air temperature -> ZT(ZoneNum)
				// For Mundt model, average room air is the weighted value of floor and ceiling air temps
				TRoomAverage = ( LineNode( MundtZoneNum, MundtCeilAirID ).Temp + LineNode( MundtZoneNum, MundtFootAirID ).Temp ) / 2;
				DeltaTemp = TRoomAverage - LineNode( MundtZoneNum, TstatNodeID ).Temp;
				// ZT(ZoneNum) = TempZoneThermostatSetPoint(ZoneNum) + DeltaTemp
				// c) Leaving-zone air temperature -> Node(ZoneNode)%Temp
				ZoneNodeNum = Zone( ZoneNum ).SystemZoneNodeNumber;
				DeltaTemp = LineNode( MundtZoneNum, ReturnNodeID ).Temp - LineNode( MundtZoneNum, TstatNodeID ).Temp;
				Node( ZoneNodeNum ).Temp = TempZoneThermostatSetPoint( ZoneNum ) + DeltaTemp;
				// d) Thermostat air temperature -> TempTstatAir(ZoneNum)
				TempTstatAir( ZoneNum ) = ZT( ZoneNum ); // for indirect coupling, control air temp is equal to mean air temp?
			}
			// set flag to indicate that Mundt model is used for this zone at the present time
			AirModel( ZoneNum ).SimAirModel = true;
		} else { // Controlled zone when the system is off --> Use the mixing model instead of the Mundt model
			// Bulk air temperatures -> TempEffBulkAir(SurfNum)
			for ( SurfNum = 1; SurfNum <= NumOfSurfs; ++SurfNum ) {
				TempEffBulkAir( SurfFirst + SurfNum - 1 ) = MAT( ZoneNum );
				// set flag for reference air temperature
				Surface( SurfFirst + SurfNum - 1 ).TAirRef = ZoneMeanAirTemp;
			}
			// set flag to indicate that Mundt model is NOT used for this zone at the present time
			AirModel( ZoneNum ).SimAirModel = false;
		}

	}

	//*****************************************************************************************

	//     NOTICE

	//     Copyright © 1996-2015 The Board of Trustees of the University of Illinois
	//     and The Regents of the University of California through Ernest Orlando Lawrence
	//     Berkeley National Laboratory.  All rights reserved.

	//     Portions of the EnergyPlus software package have been developed and copyrighted
	//     by other individuals, companies and institutions.  These portions have been
	//     incorporated into the EnergyPlus software package under license.   For a complete
	//     list of contributors, see "Notice" located in main.cc.

	//     NOTICE: The U.S. Government is granted for itself and others acting on its
	//     behalf a paid-up, nonexclusive, irrevocable, worldwide license in this data to
	//     reproduce, prepare derivative works, and perform publicly and display publicly.
	//     Beginning five (5) years after permission to assert copyright is granted,
	//     subject to two possible five year renewals, the U.S. Government is granted for
	//     itself and others acting on its behalf a paid-up, non-exclusive, irrevocable
	//     worldwide license in this data to reproduce, prepare derivative works,
	//     distribute copies to the public, perform publicly and display publicly, and to
	//     permit others to do so.

	//     TRADEMARKS: EnergyPlus is a trademark of the US Department of Energy.

} // MundtSimMgr

} // EnergyPlus
