/*
 * This macro shows a minimum working example of running track fitting over
 * the production cluster and track seed DSTs.. There are some analysis 
 * modules run at the end which package clusters, and clusters on tracks 
 * into trees for analysis.
 */

#include <fun4all/Fun4AllUtils.h>
#include <G4_ActsGeom.C>
#include <G4_Global.C>
#include <G4_Magnet.C>
#include <GlobalVariables.C>
#include <QA.C>
#include <Trkr_Clustering.C>
#include <Trkr_Reco.C>
#include <Trkr_RecoInit.C>
#include <Trkr_TpcReadoutInit.C>

#include <ffamodules/CDBInterface.h>

#include <fun4all/Fun4AllDstInputManager.h>
#include <fun4all/Fun4AllDstOutputManager.h>
#include <fun4all/Fun4AllInputManager.h>
#include <fun4all/Fun4AllOutputManager.h>
#include <fun4all/Fun4AllRunNodeInputManager.h>
#include <fun4all/Fun4AllServer.h>

#include <phool/recoConsts.h>

#include <cdbobjects/CDBTTree.h>

#include <tpccalib/PHTpcResiduals.h>

#include <trackingqa/SiliconSeedsQA.h>
#include <trackingqa/TpcSeedsQA.h>
#include <trackingqa/TpcSiliconQA.h>

#include <trackingdiagnostics/TrackResiduals.h>
#include <trackingdiagnostics/TrkrNtuplizer.h>

#include <stdio.h>

R__LOAD_LIBRARY(libfun4all.so)
R__LOAD_LIBRARY(libffamodules.so)
R__LOAD_LIBRARY(libphool.so)
R__LOAD_LIBRARY(libcdbobjects.so)
R__LOAD_LIBRARY(libTrackingDiagnostics.so)
R__LOAD_LIBRARY(libtrackingqa.so)
void Fun4All_TrackAnalyzer(
    const int nEvents = 10,
    const std::string trackfilename = "/sphenix/lustre01/sphnxpro/production/run2pp/physics/ana494_2024p021_v001/DST_TRKR_TRACKS/run_00053800_00053900/dst/DST_TRKR_TRACKS_run2pp_ana494_2024p021_v001-00053877-00000.root",
    const std::string seedfilename = "/sphenix/lustre01/sphnxpro/production/run2pp/physics/ana494_2024p021_v001/DST_TRKR_SEED/run_00053800_00053900/dst/DST_TRKR_SEED_run2pp_ana494_2024p021_v001-00053877-00000.root",
    const std::string clusterfilename = "/sphenix/lustre01/sphnxpro/production/run2pp/physics/ana494_2024p021_v001/DST_TRKR_CLUSTER/run_00053800_00053900/dst/DST_TRKR_CLUSTER_run2pp_ana494_2024p021_v001-00053877-00000.root",
    const std::string outfilename = "clusters_seeds")
{

  std::pair<int, int>
      runseg = Fun4AllUtils::GetRunSegment(seedfilename);
  int runnumber = runseg.first;
  int segment = runseg.second;

  std::cout << " run: " << runnumber
            << " samples: " << TRACKING::reco_tpc_maxtime_sample
            << " pre: " << TRACKING::reco_tpc_time_presample
            << " vdrift: " << G4TPC::tpc_drift_velocity_reco
            << std::endl;

  // distortion calibration mode
  /*
   * set to true to enable residuals in the TPC with
   * TPC clusters not participating to the ACTS track fit
   */
  G4TRACKING::SC_CALIBMODE = false;
  Enable::MVTX_APPLYMISALIGNMENT = true;
  ACTSGEOM::mvtx_applymisalignment = Enable::MVTX_APPLYMISALIGNMENT;
  TRACKING::pp_mode = true;
  
  TString outfile = outfilename + "_" + runnumber + "-" + segment + ".root";

  std::string theOutfile = outfile.Data();

  auto se = Fun4AllServer::instance();
  se->Verbosity(1);

  auto rc = recoConsts::instance();
  rc->set_IntFlag("RUNNUMBER", runnumber);

  Enable::CDB = true;
  rc->set_StringFlag("CDB_GLOBALTAG", "newcdbtag");
  rc->set_uint64Flag("TIMESTAMP", runnumber);
  std::string geofile = CDBInterface::instance()->getUrl("Tracking_Geometry");

  Fun4AllRunNodeInputManager *ingeo = new Fun4AllRunNodeInputManager("GeoIn");
  ingeo->AddFile(geofile);
  se->registerInputManager(ingeo);

  TpcReadoutInit( runnumber );
  // these lines show how to override the drift velocity and time offset values set in TpcReadoutInit
  // G4TPC::tpc_drift_velocity_reco = 0.0073844; // cm/ns
  // TpcClusterZCrossingCorrection::_vdrift = G4TPC::tpc_drift_velocity_reco;
  // G4TPC::tpc_tzero_reco = -5*50;  // ns

  G4TPC::ENABLE_MODULE_EDGE_CORRECTIONS = true;

  // to turn on the default static corrections, enable the two lines below
  G4TPC::ENABLE_STATIC_CORRECTIONS = true;
  G4TPC::USE_PHI_AS_RAD_STATIC_CORRECTIONS = false;

  //to turn on the average corrections, enable the three lines below
  //note: these are designed to be used only if static corrections are also applied
  G4TPC::ENABLE_AVERAGE_CORRECTIONS = true;
  G4TPC::USE_PHI_AS_RAD_AVERAGE_CORRECTIONS = false;
   // to use a custom file instead of the database file:
  G4TPC::average_correction_filename = CDBInterface::instance()->getUrl("TPC_LAMINATION_FIT_CORRECTION");
  G4MAGNET::magfield_rescale = 1;
  TrackingInit();

  auto hitsinseed = new Fun4AllDstInputManager("SeedInputManager");
  hitsinseed->fileopen(seedfilename);
  se->registerInputManager(hitsinseed);

  auto hitsinclus = new Fun4AllDstInputManager("ClusterInputManager");
  hitsinclus->fileopen(clusterfilename);
  se->registerInputManager(hitsinclus);

  auto hitsintrack = new Fun4AllDstInputManager("TrackInputManager");
  hitsintrack->fileopen(trackfilename);
  se->registerInputManager(hitsintrack);

  TString residoutfile = theOutfile + "_resid.root";
  std::string residstring(residoutfile.Data());

  auto resid = new TrackResiduals("TrackResiduals");
  resid->outfileName(residstring);
  resid->alignment(false);

  // adjust track map name
  if (G4TRACKING::SC_CALIBMODE && !G4TRACKING::convert_seeds_to_svtxtracks)
  {
    resid->trackmapName("SvtxSiliconMMTrackMap");
    if (G4TRACKING::SC_USE_MICROMEGAS)
    {
      resid->set_doMicromegasOnly(true);
    }
  }

  resid->clusterTree();
  resid->convertSeeds(G4TRACKING::convert_seeds_to_svtxtracks);
  resid->Verbosity(0);
  se->registerSubsystem(resid);

  if (Enable::QA)
  {
    se->registerSubsystem(new SiliconSeedsQA);
    se->registerSubsystem(new TpcSeedsQA);
    se->registerSubsystem(new TpcSiliconQA);
  }
  se->run(nEvents);
  se->End();
  se->PrintTimer();

  std::cout << "CDB Files used:" << std::endl;
  CDBInterface::instance()->Print();
  
  if (Enable::QA)
  {
    TString qaname = theOutfile + "_qa.root";
    std::string qaOutputFileName(qaname.Data());
    QAHistManagerDef::saveQARootFile(qaOutputFileName);
  }

  delete se;
  std::cout << "Finished" << std::endl;
  gSystem->Exit(0);
}
