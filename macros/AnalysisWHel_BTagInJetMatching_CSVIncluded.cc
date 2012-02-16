
#include <cmath>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

// Root stuff
#include "TROOT.h"
#include "TH1F.h"
#include "TFile.h"
#include "TStyle.h"
#include "TF2.h"
#include "TGraphErrors.h"
#include "TGraphAsymmErrors.h"
#include "TBranch.h"
#include "TTree.h"
#include "TClonesArray.h"
#include "TDirectory.h"
#include "TNtuple.h"
#include "TProfile.h"

#include "../Tools/interface/PlottingTools.h"
#include "../Tools/interface/MultiSamplePlot.h"
#include "../Content/interface/AnalysisEnvironment.h"
#include "../Content/interface/Dataset.h"
#include "../WHelicities/interface/WTree.h"
#include "../JESMeasurement/interface/FullKinFit.h"
#include "../MCInformation/interface/LumiReWeighting.h"
#include "../MCInformation/interface/Lumi3DReWeighting.h"
#include "../Selection/interface/SelectionTable.h"

#include "Style.C"

//Includes for made classes:
#include "../WHelicities/interface/BTagName_CSV.h"
#include "../WHelicities/interface/BTagJetSelection_CSV.h"
#include "../WHelicities/interface/BTagCosThetaCalculation.h"
//#include "../WHelicities/interface/MinuitFitter.h"

//Includes for Minuit
#include <TMinuitMinimizer.h>
//#include "TNtuple.h"  //A simple tree restricted to a list of float variables only. 

const int ndimen = 3;

const int CosThetaBinNumber = 15;       
const int NumberSSVHP=14;
const int NumberSSVHE=14;
const int NumberTCHP=14;
const int NumberTCHE=14;
const int NumberCSV=14;
int NumberOfHelicityBins=100; 
TNtuple *genttbarhisto[CosThetaBinNumber];  //This is the vector of ntuples containing the generated values of cos theta* for each cos theta* reconstructed bin

//new version received from Mara
class MyChi2 : public ROOT::Math::IMultiGenFunction {
public:
  // Mandatory functions 
  
  virtual MyChi2 * Clone() const {return new MyChi2;};
  virtual unsigned int NDim() const {return ndimen;}
  
  // Constructors
  MyChi2(){};
  MyChi2(TH1F* & datah, TH1F* &signalh, TH1F* & bkgh,double ff0, double ffl, double ffr) : datah_(datah),signalh_(signalh),bkgh_(bkgh),ff0_(ff0),ffl_(ffl),ffr_(ffr){};  

  double DoEval(const double* x) const {


    double f0Fit = x[0];
    double flFit = x[1];
    double frFit = 0;
    double nn=1.;
    if (ndimen==3) { 
      frFit = 1-x[0]-x[1]; 
      nn = x[2];
    }
    else if (ndimen==2){
      flFit = 1.-x[0];
      nn = x[1];
    }

    double ChiSquaredAllBins =0.;
    double chi2 = 0.;
    
    int nncells = signalh_->GetSize()-2;  // -1 and not -2, to get the last bin

    // initialization
    double nmcaux[nncells];
    for (int ibin=0; ibin< nncells; ibin++){
      nmcaux[ibin]=0.;
    }
  
    // loop over ntuples containing gen-level costh info relative to each costhRec bin
    Int_t nGENentries[nncells];
    for (int ihgen=0; ihgen<nncells; ihgen++){ 
 
      // Here I am assuming that the input array only contains entries that 
      // have costheta* reconstructed
      // If no costhetarec for this igen, one should skip this entry in the sum
      // or send it to an underflow/overflow bin
      
      Float_t costhgen, evtweight;
      nGENentries[ihgen] = genttbarhisto[ihgen]->GetEntries();
      
      genttbarhisto[ihgen]->SetBranchAddress("costhgen",&costhgen);
      genttbarhisto[ihgen]->SetBranchAddress("evtweight",&evtweight);
    
      if (nGENentries[ihgen] ==0) {
	nmcaux[ihgen] =0;
	//unrwgt[ihgen]=0; //Double which has the function of summing up the number of generated events after normalization, but before reweighting in the fitter.
	// Double is necessary because cos theta * only needs to be reweighted for generated Muon leptonic decays
	// --> Not needed in my code since we can split in ttbarSemiMu ??
      }
      else{
        for (Int_t igen=0; igen<nGENentries[ihgen]; ++igen) {
          genttbarhisto[ihgen]->GetEntry(igen);
          double xx = costhgen;
          double thisevweight = evtweight;

          double SM = 0.3325*(1-xx)*(1-xx)*3/8+0.6671*(1-xx*xx)*3/4 + 0.0004*(1+xx)*(1+xx)*3/8 ;
          double newmodel = flFit*(1-xx)*(1-xx)*3/8+f0Fit*(1-xx*xx)*3/4 + frFit*(1+xx)*(1+xx)*3/8 ;
          
          nmcaux[ihgen] +=thisevweight*newmodel/SM;
        }
      }	  
    }
    for (int ibin=0; ibin< nncells; ibin++){
      double ndata_i = datah_->GetBinContent(ibin+1);       
      double nbkg_i = bkgh_->GetBinContent(ibin+1);     
      double  nmc_i =  nbkg_i +  nn*nmcaux[ibin] ;   // nn--> free normalization found by the Fit
      
      if (nmc_i>0){
	chi2 += ( nmc_i - ndata_i * TMath::Log(nmc_i) ); 
	ChiSquaredAllBins=ChiSquaredAllBins+(((nmc_i-ndata_i)/(sqrt(ndata_i)))*((nmc_i-ndata_i)/(sqrt(ndata_i))));//nmc_i = all samples ndata_ = semiMu	
      }      
    }    
    return ChiSquaredAllBins;
  }
private:
  
  TH1F* datah_;
  TH1F* signalh_;
  TH1F* bkgh_;
  double ff0_;
  double ffl_;
  double ffr_;
};

using namespace std;
using namespace reweight;

/// TGraphAsymmErrors
map<string,TGraphAsymmErrors*> graphAsymmErr;
map<string,TGraphErrors*> graphErr;

/// Normal Plots (TH1F* and TH2F*)
map<string,TH1F*> histo1D;
map<string,TH2F*> histo2D;

/// MultiSamplePlot
map<string,MultiSamplePlot*> MSPlot;

int main (int argc, char *argv[])
{
  clock_t start = clock();

  cout << "*******************************************************" << endl;
  cout << " Beginning of the program for W Helicities Analysis " << endl;
  cout << "   --> bTag in jet-quark matching ! " << endl;
  cout << "*******************************************************" << endl;
 
  //  setTDRStyle();
  setMyStyle();
  
  string pathPNG = "Plots/bTagUsedInJetMatching/";
  mkdir(pathPNG.c_str(),0777);
  mkdir((pathPNG+"MSPlot/").c_str(),0777);
  mkdir((pathPNG+"CosThetaPlots/").c_str(),0777);
  
  float Luminosity;
  
  vector<string> inputWTree;
  vector<string> nameDataSet;

  //--------------------------------------------------------------
  //     DataSamples needed for 2011RunA for muon channel:      --
  //--------------------------------------------------------------
  
  string UsedTrigger;
  bool IsoMu172024Trigger = false;
  bool TriCentralJet30Trigger = true;
  if(IsoMu172024Trigger == true){
    UsedTrigger = "IsoMu172024Trigger";
    Luminosity = 2141.96; 
  }
  else if(TriCentralJet30Trigger == true){
    UsedTrigger = "TriCentralJet30Trigger";
    Luminosity = 2145.959; 
  }
  cout << "Executing the W Helicities analysis for an integrated luminosity of " << Luminosity << " pb^-1" << endl;

  //Probability cut value:
  float KinFitCut = 0.;

  string decayChannel;
  bool semiMuon = true;
  bool semiElectron = false;
  if(semiMuon == true){decayChannel = "SemiMu";}
  else if(semiElectron == true){decayChannel = "SemiEl";}
  
  //1) Nominal samples:
  //inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_Data_"+decayChannel+".root").c_str());
  //nameDataSet.push_back("Data");
  inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_ST_SingleTop_tChannel_tbar_"+decayChannel+".root").c_str());
  nameDataSet.push_back("ST_SingleTop_tChannel_tbar");
  inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_ST_SingleTop_tChannel_t_"+decayChannel+".root").c_str());
  nameDataSet.push_back("ST_SingleTop_tChannel_t");
  inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_ST_SingleTop_tWChannel_tbar_"+decayChannel+".root").c_str());
  nameDataSet.push_back("ST_SingleTop_tWChannel_tbar");
  inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_ST_SingleTop_tWChannel_t_"+decayChannel+".root").c_str());
  nameDataSet.push_back("ST_SingleTop_tWChannel_t");
  inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_TTbarJets_Other_"+decayChannel+".root").c_str());
  nameDataSet.push_back("TTbarJets_Other");
  inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_TTbarJets_SemiEl_"+decayChannel+".root").c_str());
  nameDataSet.push_back("TTbarJets_SemiEl");
  inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_WJets_"+decayChannel+".root").c_str());
  nameDataSet.push_back("WJets_Nominal");
  inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_ZJets_"+decayChannel+".root").c_str());
  nameDataSet.push_back("ZJets");

  //2) JES Plus/Min samples:

//   inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESMinus_1Sig_ST_SingleTop_tChannel_tbar_"+decayChannel+".root").c_str());
//   nameDataSet.push_back("JES_ST_SingleTop_tChannel_tbar");
//   inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESMinus_1Sig_ST_SingleTop_tChannel_t_"+decayChannel+".root").c_str());
//   nameDataSet.push_back("JES_ST_SingleTop_tChannel_t");
//   inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESMinus_1Sig_ST_SingleTop_tWChannel_tbar_"+decayChannel+".root").c_str());
//   nameDataSet.push_back("JES_ST_SingleTop_tWChannel_tbar");
//   inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESMinus_1Sig_ST_SingleTop_tWChannel_t_"+decayChannel+".root").c_str());
//   nameDataSet.push_back("JES_ST_SingleTop_tWChannel_t");
//   inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESMinus_1Sig_TTbarJets_Other_"+decayChannel+".root").c_str());
//   nameDataSet.push_back("JES_TTbarJets_Other");
//   inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESMinus_1Sig_WJets_"+decayChannel+".root").c_str());
//   nameDataSet.push_back("JES_WJets");
//   inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESMinus_1Sig_ZJets_"+decayChannel+".root").c_str());
//   nameDataSet.push_back("JES_ZJets");
//   inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESMinus_1Sig_TTbarJets_SemiMuon_"+decayChannel+".root").c_str());
//   nameDataSet.push_back("JES_TTbarJets_SemiMuon");
  
    inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESPlus_1Sig_ST_SingleTop_tChannel_tbar_"+decayChannel+".root").c_str());
    nameDataSet.push_back("JES_ST_SingleTop_tChannel_tbar");
    inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESPlus_1Sig_ST_SingleTop_tChannel_t_"+decayChannel+".root").c_str());
    nameDataSet.push_back("JES_ST_SingleTop_tChannel_t");
    inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESPlus_1Sig_ST_SingleTop_tWChannel_tbar_"+decayChannel+".root").c_str());
    nameDataSet.push_back("JES_ST_SingleTop_tWChannel_tbar");
    inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESPlus_1Sig_ST_SingleTop_tWChannel_t_"+decayChannel+".root").c_str());
    nameDataSet.push_back("JES_ST_SingleTop_tWChannel_t");
    inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESPlus_1Sig_TTbarJets_Other_"+decayChannel+".root").c_str());
    nameDataSet.push_back("JES_TTbarJets_Other");
    inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESPlus_1Sig_WJets_"+decayChannel+".root").c_str());
    nameDataSet.push_back("JES_WJets");
    inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESPlus_1Sig_ZJets_"+decayChannel+".root").c_str());
    nameDataSet.push_back("JES_ZJets");
    inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_JESPlus_1Sig_TTbarJets_SemiMuon_"+decayChannel+".root").c_str());
    nameDataSet.push_back("JES_TTbarJets_SemiMuon");
  
  //3) WJets systematics:  
  //inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_WJets_"+decayChannel+".root").c_str());  
  //nameDataSet.push_back("Syst_WJets");
  
  //TTbarJets_SemiMuon sample should always be put as latest sample to avoid crash of TMinuitMinimizer !!
  inputWTree.push_back(("WTree/KinFit_WTree_"+UsedTrigger+"_TTbarJets_SemiMuon_"+decayChannel+".root").c_str());
  nameDataSet.push_back("TTbarJets_SemiMuon");

  TFile *fout = new TFile (("WHelicities_Analysis_"+UsedTrigger+".root").c_str(), "RECREATE");
  fout->cd();

  //Create .txt file to write away all information obtained for bTag study
  mkdir("BTagPerformanceStudy/",0777);
  //ofstream bTagFile("BTagPerformanceStudy/BTagInMatchingOutput.txt");
  //ofstream bTagFileTex("../../../../Documents/Vub/PhD/BTagPerformanceStudy/bTagInMatching.tex");

  //-----------------------------------------------
  //  Output .tex files for presentation/paper
  //-----------------------------------------------
  //PresentationTex.precision(2);

  //oooooOOOOOOOOOOOoooooooooooOOOOOOOOOOOOOOOOoooooooooOOOOOOOOOOOoooooooooooooooooOOOOOOOOOOOOOOOoooooooooooooOOOOOOOOOOOoooooooo
  //1) Own results: No btag at event selection, top mass constraint for leptonic top in KinFit and offline muon pt cut of 27
  //oooooOOOOOOOOOOOoooooooooooOOOOOOOOOOOOOOOOoooooooooOOOOOOOOOOOoooooooooooooooooOOOOOOOOOOOOOOOoooooooooooooOOOOOOOOOOOoooooooo

  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFitted.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedProbCut.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedMonteCarlo.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedJESMin.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedJESMinProbCut.tex").c_str());
  ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedJESPlus.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedJESPlusProbCut.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedWMin.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedWMin100.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedWMinProbCut.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedWPlus.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedWPlus100.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/WorkshopPresentation"+UsedTrigger+"NoBTagEvtSelTopFittedWPlusProbCut.tex").c_str());

  //oooooOOOOOOOOOOOoooooooooooOOOOOOOOOOOOOOOOoooooooooOOOOOOOOOOOoooooooooooooooooOOOOOOOOOOOOOOOoooooooooooooOOOOOOOOOOOoooooooo
  //2) Ciemat results: SSVHEM btag at event selection, leptonic top mass left free in KinFit and offline muon pt cut of 25
  //oooooOOOOOOOOOOOoooooooooooOOOOOOOOOOOOOOOOoooooooooOOOOOOOOOOOoooooooooooooooooOOOOOOOOOOOOOOOoooooooooooooOOOOOOOOOOOoooooooo

  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/CiematTest"+UsedTrigger+".tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/CiematMonteCarlo"+UsedTrigger+".tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/Ciemat"+UsedTrigger+"JESMin.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/Ciemat"+UsedTrigger+"JESPlus.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/Ciemat"+UsedTrigger+"WMin100.tex").c_str());
  //ofstream PresentationTex(("BTagPerformanceStudy_CSV/Ciemat"+UsedTrigger+"WPlus100.tex").c_str());

  //-----------------------------------------
  //  Start of filling of .tex files !!
  //-----------------------------------------
  PresentationTex << " \\begin{table} " << endl;
  PresentationTex << " \\begin{tiny} " << endl;
  PresentationTex << " \\renewcommand{\\arraystretch}{1.2} " << endl;
  PresentationTex << " \\begin{center} " << endl;
  for(int ii=0; ii<nameDataSet.size(); ii++){
    if(ii==0){PresentationTex << "  \\begin{tabular}{|c|";}
    else if(ii < (nameDataSet.size()-1)){PresentationTex << "c|";}
    else{PresentationTex << "c|c|c|c|c|c|c|c|c|c|c|} " << endl;}
  }
  PresentationTex << " \\hline " << endl;
  for(int ii=0; ii < nameDataSet.size();ii++){
    PresentationTex << " & " << nameDataSet[ii];
  }
  PresentationTex << " & bLept correct & F+ & F+ - SM & $\\delta$ F+ & F- & F- - SM & $\\delta$ F- & F0 & F0 - SM & $\\delta$ F0 \\\\ " << endl;
  PresentationTex << " \\hline " << endl;

  // initialize histograms
  cout << "Initializing histograms" << endl;

  histo1D["lumiWeights3D"]= new TH1F("lumiWeights3D","lumiWeights3D",25,0,50);
  
  //Standard Model helicity values:
  float SMfrResult = 0.0334506;
  float SMflResult = 0.321241;
  float SMf0Result = 0.64491;

  //Zie code Stijn voor alle gebruikte controle plots !

  histo1D["KinFitProbCorrectBLept"] = new TH1F("KinFitProbCorrectBLept","KinFitProbCorrectBLept",25,0,1);
  histo1D["KinFitProbWrongBLept"] = new TH1F("KinFitProbWrongBLept","KinFitProbWrongBLept",25,0,1);

  histo1D["KinFitProbCorrectBLeptSSVHEM"] = new TH1F("KinFitProbCorrectBLeptSSVHEM","KinFitProbCorrectBLeptSSVHEM",25,0,1);
  histo1D["KinFitProbWrongBLeptSSVHEM"] = new TH1F("KinFitProbWrongBLeptSSVHEM","KinFitProbWrongBLeptSSVHEM",25,0,1);

  histo1D["CosThetaCorrectBLept"]=new TH1F("CosThetaCorrectBLept","CosThetaCorrectBLept",CosThetaBinNumber,-1,1);
  histo1D["CosThetaWrongBLept"]=new TH1F("CosThetaWrongBLept","CosThetaWrongBLept",CosThetaBinNumber,-1,1);
  histo1D["CosThetaCorrectBLeptSSVHEM"]=new TH1F("CosThetaCorrectBLeptSSVHEM","CosThetaCorrectBLeptSSVHEM",CosThetaBinNumber,-1,1);
  histo1D["CosThetaWrongBLeptSSVHEM"]=new TH1F("CosThetaWrongBLeptSSVHEM","CosThetaWrongBLeptSSVHEM",CosThetaBinNumber,-1,1);
  
  histo1D["CosThetaResSSVHEMLept"] = new TH1F("CosThetaResSSVHEMLept","CosThetaResSSVHEMLept",200,-2,2);
  histo1D["CosThetaResNobTag"] = new TH1F("CosThetaResNobTag","CosThetaResNobTag",200,-2,2);

  float XSection;
  float EqLumi;
  vector<Dataset*> datasets; // needed for MSPlots
  for(unsigned int iDataSet=0; iDataSet<inputWTree.size(); iDataSet++){
    TFile* inFile = new TFile(inputWTree[iDataSet].c_str(),"READ");
    TTree* inConfigTree = (TTree*) inFile->Get("configTreeWTreeFile");
    TBranch* d_br = (TBranch*) inConfigTree->GetBranch("Dataset");
    TClonesArray* tc_dataset = new TClonesArray("Dataset",0);
    d_br->SetAddress(&tc_dataset);
    inConfigTree->GetEvent(0);
    Dataset* dataSet = (Dataset*) tc_dataset->At(0);
    int color = 0;
    XSection = dataSet->Xsection();
    EqLumi = dataSet->EquivalentLumi();    

    //Nominal samples:
    if( dataSet->Name().find("TTbarJets_SemiMuon") == 0 && dataSet->Name().find("JES") != 0) color = kRed+1;
    if( dataSet->Name().find("TTbarJets_SemiEl") == 0 && dataSet->Name().find("JES") != 0) color = kRed-4;
    if( dataSet->Name().find("TTbarJets_Other") == 0 && dataSet->Name().find("JES") != 0 ) color = kRed-7;
    if( dataSet->Name().find("WJets") == 0 )
      {
	dataSet->SetTitle("W#rightarrowl#nu");
	color = kGreen-3;
      }
    if( dataSet->Name().find("ZJets") == 0 && dataSet->Name().find("JES") != 0 )
      {
	dataSet->SetTitle("Z/#gamma*#rightarrowl^{+}l^{-}");
	color = kAzure-2;
      }
    if( dataSet->Name().find("ST") == 0 && dataSet->Name().find("JES") != 0 ) color = kMagenta;

    //Systematics samples:
    //JES:
    if( dataSet->Name().find("JES_TTbarJets_SemiMuon") == 0){ 
      color = kRed+1;
      dataSet->SetTitle("t#bar{t}+jets semi-#mu (JES)");
    }
    if( dataSet->Name().find("JES_TTbarJets_SemiEl") == 0){ 
      color = kRed-4;
      dataSet->SetTitle("t#bar{t}+jets semi-el (JES)");
    }
    if( dataSet->Name().find("JES_TTbarJets_Other") == 0){ 
      color = kRed-7;
      dataSet->SetTitle("t#bar{t}+jets other (JES)");
    }
    if( dataSet->Name().find("JES_WJets") == 0 )
      {
	dataSet->SetTitle("W#rightarrowl#nu (JES)");
	color = kGreen-3;
      }
    if( dataSet->Name().find("JES_ZJets") == 0)
      {
	dataSet->SetTitle("Z/#gamma*#rightarrowl^{+}l^{-} (JES)");
	color = kAzure-2;
      }
    if( dataSet->Name().find("JES_ST") == 0){ 
      color = kMagenta;
      dataSet->SetTitle("Single-Top (JES)");
    }
    
    //WJets scale up/down
    if( dataSet->Name().find("Syst_WJets") == 0 )
      {
	dataSet->SetTitle("W#rightarrowl#nu (WJets up/down)");
	color = kGreen-3;
      }    

    Dataset* tmpDS = new Dataset(dataSet->Name(), dataSet->Title(), dataSet->DoIt(), color, dataSet->LineStyle(), dataSet->LineWidth(), dataSet->NormFactor(), XSection);
    tmpDS->SetEquivalentLuminosity( EqLumi );
    datasets.push_back( tmpDS );
  }
  
  //MSPlots
  MSPlot["CosThetaSSVHEMLept"]= new MultiSamplePlot(datasets, "CosThetaSSVHEMLept", CosThetaBinNumber,-1,1,"CosThetaSSVHEMLept");  
  MSPlot["KinFitProbSSVHEMLept"]= new MultiSamplePlot(datasets, "KinFitProbSSVHEMLept", CosThetaBinNumber,0,1,"KinFitProbSSVHEMLept");  
  MSPlot["CosThetaTCHEMLept"]= new MultiSamplePlot(datasets, "CosThetaTCHEMLept", CosThetaBinNumber,-1,1,"CosThetaTCHEMLept");  
  MSPlot["KinFitProbTCHEMLept"]= new MultiSamplePlot(datasets, "KinFitProbTCHEMLept", CosThetaBinNumber,0,1,"KinFitProbTCHEMLept");  
  MSPlot["CosThetaTCHPMLept"]= new MultiSamplePlot(datasets, "CosThetaTCHPMLept", CosThetaBinNumber,-1,1,"CosThetaTCHPMLept");  
  MSPlot["CosThetaSSVHPMLept"]= new MultiSamplePlot(datasets, "CosThetaSSVHPMLept", CosThetaBinNumber,-1,1,"CosThetaSSVHPMLept");  
  MSPlot["KinFitProbSSVHPMLept"]= new MultiSamplePlot(datasets, "KinFitProbSSVHPMLept", CosThetaBinNumber,0,1,"KinFitProbSSVHPMLept");  
  MSPlot["CosThetaCSVMLept"]= new MultiSamplePlot(datasets, "CosThetaCSVMLept", CosThetaBinNumber,-1,1,"CosThetaCSVMLept");
 
  //Check nPrimary vertices for different executed cuts !!
  MSPlot["nPrimaryVert"] = new MultiSamplePlot(datasets,"nPrimaryVert" , 20, 0, 20, "nPrimaryVert");
  MSPlot["nPVBeforeCuts"] = new MultiSamplePlot(datasets,"nPVBeforeCuts" , 20, 0, 20, "nPVBeforeCuts");
  MSPlot["nPVAfterSSVHEMbTag"] = new MultiSamplePlot(datasets,"nPVAfterSSVHEMbTag" , 20, 0, 20, "nPVAfterSSVHEMbTag");
  MSPlot["nPVAfterMuon27Cut"] = new MultiSamplePlot(datasets,"nPVAfterMuon27Cut" , 20, 0, 20, "nPVAfterMuon27Cut");
  MSPlot["nPVAfterTransverseMassCut"] = new MultiSamplePlot(datasets,"nPVAfterTransverseMassCut" , 20, 0, 20, "nPVAfterTransverseMassCut");
  MSPlot["nPVBeforeFoundJetComb"] = new MultiSamplePlot(datasets, "nPVBeforeFoundJetComb", 20, 0, 20,"nPVBeforeFoundJetComb");
  MSPlot["nPVAfterFoundJetComb"] = new MultiSamplePlot(datasets, "nPVAfterFoundJetComb", 20, 0, 20,"nPVAfterFoundJetComb");
  MSPlot["nPVAfterFoundJetCombbTag"] = new MultiSamplePlot(datasets, "nPVAfterFoundJetCombbTab", 20, 0, 20,"nPVAfterFoundJetCombbTag");
  MSPlot["nPVAfterFoundCosTheta"] = new MultiSamplePlot(datasets, "nPVAfterFoundCosTheta", 20, 0, 20,"nPVAfterFoundCosTheta");
  MSPlot["nPVAfterFoundCosThetabTag"] = new MultiSamplePlot(datasets, "nPVAfterFoundCosThetabTag", 20, 0, 20,"nPVAfterFoundCosThetabTag");

  MSPlot["TransverseMassBeforeCut"]=new MultiSamplePlot(datasets,"TransverseMassBeforeCut",150,0,200,"TransverseMassBeforeCut");
  MSPlot["TransverseMassAfterCut"]=new MultiSamplePlot(datasets,"TransverseMassAfterCut",150,0,200,"TransverseMassAfterCut"); 

  //Histograms to check differences between events with Negative and positive DSquared (And check influence of probability cut) 
  MSPlot["CosThetaNobTag"]=new MultiSamplePlot(datasets,"CosThetaNobTag" ,CosThetaBinNumber,-1,1, "CosThetaNobTag");
  MSPlot["CosThetaNobTagProbCut"]=new MultiSamplePlot(datasets,"CosThetaNobTagProbCut" ,CosThetaBinNumber*2,-2,2,"CosThetaNobTagProbCut" );
  MSPlot["KinFitProbNobTag"]=new MultiSamplePlot(datasets, "KinFitProbNobTag",25,0,1,"KinFitProbNobTag");

  MSPlot["Jet1Pt"] = new MultiSamplePlot(datasets, "Jet1Pt", 100,0,500,"Jet1Pt");
  MSPlot["Jet2Pt"] = new MultiSamplePlot(datasets, "Jet2Pt", 100,0,500,"Jet2Pt");
  MSPlot["Jet3Pt"] = new MultiSamplePlot(datasets, "Jet3Pt", 100,0,500,"Jet3Pt");
  MSPlot["Jet4Pt"] = new MultiSamplePlot(datasets, "Jet4Pt", 100,0,500,"Jet4Pt");
  
  MSPlot["MetPhi"]= new MultiSamplePlot(datasets, "MetPhi",200,-3.3,3.3,"MetPhi");
  // MSPlot["NeutrinoKinFitPhi"]= new MultiSamplePlot(datasets,"NeutrinoKinFitPhi" ,50,-3.3,3.3,"NeutrinoKinFitPhi");
  // //MSPlot["NeutrinoPhiDiff"]= new MultiSamplePlot(datasets, "NeutrinoPhiDiff",100,-7,7,"NeutrinoPhiDiff");

  MSPlot["MetPx"]= new MultiSamplePlot(datasets, "MetPx",200,-400,20000,"MetPx");
  // MSPlot["NeutrinoKinFitPx"]= new MultiSamplePlot(datasets,"NeutrinoKinFitPx" ,50,-200,200,"NeutrinoKinFitPx");
  // //MSPlot["NeutrinoPxDiff"]= new MultiSamplePlot(datasets, "NeutrinoPxDiff",100,-400,400,"NeutrinoPxDiff");

  MSPlot["MetPy"]= new MultiSamplePlot(datasets, "MetPy",200,-26000,400,"MetPy");
  // MSPlot["NeutrinoKinFitPy"]= new MultiSamplePlot(datasets,"NeutrinoKinFitPy" ,50,-200,200,"NeutrinoKinFitPy");
  // //MSPlot["NeutrinoPyDiff"]= new MultiSamplePlot(datasets, "NeutrinoPyDiff",100,-400,400,"NeutrinoPyDiff");  

  MSPlot["BLeptPtBeforeKinFit"]= new MultiSamplePlot(datasets,"BLeptPtBeforeKinFit",40,0,400,"BLeptPtBeforeKinFit");
  MSPlot["BLeptPtAfterKinFit"]= new MultiSamplePlot(datasets,"BLeptPtAfterKinFit",25,0,250,"BLeptPtAfterKinFit");
  MSPlot["MetPtBeforeKinFit"] = new MultiSamplePlot(datasets,"MetPtBeforeKinFit",20,0,200,"MetPtBeforeKinFit");
  MSPlot["MetPtAfterKinFit"] = new MultiSamplePlot(datasets,"MetPtAfterKinFit",20,0,200,"MetPtAfterKinFit");
  MSPlot["MuonPtBeforeKinFit"] = new MultiSamplePlot(datasets,"MuonPtBeforeKinFit",12,0,150,"MuonPtBeforeKinFit");
  MSPlot["MuonPtAfterKinFit"] = new MultiSamplePlot(datasets,"MuonPtAfterKinFit",12,0,150,"MuonPtAfterKinFit");
  MSPlot["WLeptPtBeforeKinFit"] = new MultiSamplePlot(datasets,"WLeptPtBeforeKinFit",25,0,250,"WLeptPtBeforeKinFit");
  MSPlot["WLeptPtAfterKinFit"] = new MultiSamplePlot(datasets,"WLeptPtAfterKinFit",25,0,250,"WLeptPtAfterKinFit");
  MSPlot["TopLeptPtBeforeKinFit"] = new MultiSamplePlot(datasets,"TopLeptPtBeforeKinFit",35,0,400,"TopLeptPtBeforeKinFit");
  MSPlot["TopLeptPtAfterKinFit"] = new MultiSamplePlot(datasets,"TopLeptPtAfterKinFit",35,0,400,"TopLeptPtAfterKinFit");

  std::cout << " MSPlots defined " << endl;
  
  // Zie Code Stijn voor alle gebruikte controle plots

  ////////////////////////////////
  //     Selection Table        // 
  ////////////////////////////////

  vector<string> CutsSelecTableMacro;
  CutsSelecTableMacro.push_back(string("offline cuts"));
  CutsSelecTableMacro.push_back(string("M SSVHE bTag"));
  CutsSelecTableMacro.push_back(string("Muon Pt Cut"));
  CutsSelecTableMacro.push_back(string("TransverseMass Cut"));

  SelectionTable selecTableMacro(CutsSelecTableMacro,datasets);
  selecTableMacro.SetLuminosity(Luminosity);

  // initialize LumiReWeighting stuff
  // Summer11 PU_S4, distribution obtained by averaging the number of interactions, taken from https://twiki.cern.ch/twiki/bin/viewauth/CMS/PileupMCReweightingUtilities
  // in each beam crossing to estimate the true mean.  THIS IS THE RECOMMENDED ONE for reweighting.
  Double_t probdistFlat10[25] = {
    0.0698146584,
    0.0698146584,
    0.0698146584,
    0.0698146584,
    0.0698146584,
    0.0698146584,
    0.0698146584,
    0.0698146584,
    0.0698146584,
    0.0698146584,
    0.0698146584,
    0.0630151648,
    0.0526654164,
    0.0402754482,
    0.0292988928,
    0.0194384503,
    0.0122016783,
    0.007207042,
    0.004003637,
    0.0020278322,
    0.0010739954,
    0.0004595759,
    0.0002229748,
    0.0001028162,
    4.58337152809607E-05
  };

  cout << " Defining MCLumi_f[25] " << endl;
   
  Double_t MCLumi_f[25] = {
    0.104109,
    0.0703573,
    0.0698445,
    0.0698254,
    0.0697054,
    0.0697907,
    0.0696751,
    0.0694486,
    0.0680332,
    0.0651044,
    0.0598036,
    0.0527395,
    0.0439513,
    0.0352202,
    0.0266714,
    0.019411,
    0.0133974,
    0.00898536,
    0.0057516,
    0.00351493,
    0.00212087,
    0.00122891,
    0.00070592,
    0.000384744,
    0.000219377
  };

  cout << " Defining TopDBDist2011Data_f[25] " << endl;
  
  Double_t TopDBDist2011Data_f[25] = {
    0.0127118660008111155,
    0.0273174253882752516,
    0.0647422373974094190,
    0.108494213975257103,
    0.140081296984992526,
    0.150411260268535935,
    0.142773479388604602,
    0.118012735306947752,
    0.0881395784021791473,
    0.0603740700218931975,
    0.0382939204454870868,
    0.0227366747939989136,
    0.0127228459417252551,
    0.00674674468025676568,
    0.00340977235841692389,
    0.00165292589154045016,
    0.000771798466244840342,
    0.000347480158040664431,
    0.000151563397272207710,
    0.0000642172483977206039,
    0.0000264962736283059724,
    0.0000106455374332742453,
    0.00000418355451211455042,
    0.00000161033109693768961,
    0.000000606815958689117662
  };

  cout << " Defining TrueDist2011_f[25] " << endl;
  
  Double_t TrueDist2011_f[25] = {
    0.019091,
    0.0293974,
    0.0667931,
    0.108859,
    0.139533,
    0.149342,
    0.138629,
    0.114582,
    0.0859364,
    0.059324,
    0.0381123,
    0.0229881,
    0.0131129,
    0.00711764,
    0.00369635,
    0.00184543,
    0.000889604,
    0.000415683,
    0.000188921,
    0.000146288,
    0.0,
    0.0,
    0.0,
    0.0,
    0.0
  };
  
  std::cout << " Values defined " << endl;
  
  vector<float> TrueDist2011, MClumi, Spring11MClumi, TopDBDist2011Data;
  for( int i=0; i<25; ++i){
    TopDBDist2011Data.push_back(TopDBDist2011Data_f[i]);
    TrueDist2011.push_back(TrueDist2011_f[i]);
    MClumi.push_back(MCLumi_f[i]);
    Spring11MClumi.push_back(probdistFlat10[i]);
  }

  cout << " Starting LumiReWeighting stuff " << endl;

  //  LumiReWeighting LumiWeightsSpring11 = LumiReWeighting(Spring11MClumi, TrueDist2011);
  //  LumiReWeighting LumiWeights = LumiReWeighting(MClumi, TopDBDist2011Data);
  //LumiReWeighting LumiWeights = LumiReWeighting("PileUpReweighting/pileup_WJets_36bins.root", "PileUpReweighting/pileup_2011Data_UpToRun173692.root", "pileup2", "pileup");
  cout << " Lumi3D Reweighting stuff " << endl;
  Lumi3DReWeighting Lumi3DWeights = Lumi3DReWeighting("PileUpReweighting/pileup_MC_Flat10PlusTail.root", "PileUpReweighting/pileup_FineBin_2011Data_UpToRun173692.root", "pileup", "pileup");
  Lumi3DWeights.weight3D_init(1.0);

  PoissonMeanShifter PShiftUp = PoissonMeanShifter(0.6); // PU-systematic
  PoissonMeanShifter PShiftDown = PoissonMeanShifter(-0.6); // PU-systematic

  /////////////////////////////////////////
  // Initializing used variables
  /////////////////////////////////////////
  float MassW=83.6103;
  float MassTop = 172.956;
  float SigmaW=11.1534;  //Obtained from gaussian fit on Top and W distribution with simulated information
  float SigmaTop=18.232;

  std::string bTagValues[14];
  for(int ii=1;ii<=14;ii++){
    std::stringstream out;
    out << ii;
    bTagValues[ii-1]  =  out.str();
  }

  int NumberTCHEbTags = 13;
  int NumberTCHPbTags = 13;
  int NumberSSVHEbTags = 13;
  int NumberSSVHPbTags=13;
  int NumberCSVbTags=13;
  int TotalNumberbTags = NumberTCHEbTags + 1 + NumberTCHPbTags+1 + NumberSSVHEbTags + 1 + NumberSSVHPbTags + 1 + NumberCSVbTags + 1;
  
  std::string bTagFileOutput[TotalNumberbTags];
  std::string PresentationOutput[TotalNumberbTags];
  int NumberRemainingEvents[TotalNumberbTags][inputWTree.size()];
  int NumberRemainingEventsOrigKins[TotalNumberbTags][inputWTree.size()];
  int NumberBLeptCorrectEvents[TotalNumberbTags][inputWTree.size()];
  int NumberBLeptCorrectEventsOrigKins[TotalNumberbTags][inputWTree.size()];
  //Initialize:
  for(int ii=0;ii<14;ii++){
    for(int jj=0;jj<14;jj++){
      for(int kk=0;kk<14;kk++){
	for(int ll=0;ll<14;ll++){
	  for(int nn=0;nn<14;nn++){
	    bTagFileOutput[ii+jj+kk+ll+nn]=" Wrong entry chosen";
	    PresentationOutput[ii+jj+kk+ll+nn]=" Wrong entry chosen";
	    for(int mm=0;mm<inputWTree.size();mm++){
	      NumberRemainingEvents[ii+jj+kk+ll+nn][mm]=0;
	      NumberRemainingEventsOrigKins[ii+jj+kk+ll+nn][mm]=0;
	      NumberBLeptCorrectEvents[ii+jj+kk+ll+nn][mm]=0;
	      NumberBLeptCorrectEventsOrigKins[ii+jj+kk+ll+nn][mm]=0;
	    }
	  }
	}
      }
    }
  }

  //Call class made for bTagFileOutput name giving:
  BTagName_CSV bTagName = BTagName_CSV();  
  BTagJetSelection_CSV bTagJetSelection = BTagJetSelection_CSV();
  BTagCosThetaCalculation bTagCosThetaCalculation = BTagCosThetaCalculation();

  int LengthOfPresentationArray=0;

  int NumberSelectedEvents =0;
  int NumberEventsBeforeCuts = 0;
  int NumberEventsAfterbTag = 0;
  int NumberEventsAfterTransverseMass = 0;
  int NumberUsedEvents = 0;
  int NumberUsedCorrectEvents = 0;
  int NumberUsedWrongEvents = 0;
  int NumberUsedDataEvents = 0;
  int NumberSelectedDataEvents = 0;
  int NumberDataEventsBeforeCuts = 0;

  /////////////////////////////////////////
  // Loop on datasets
  /////////////////////////////////////////
  
  for(unsigned int iDataSet=0; iDataSet<inputWTree.size(); iDataSet++){

    // string dataSetName = datasets[iDataSet]->Name();
    string dataSetName = nameDataSet[iDataSet];
    std::cout << " dataSetName : " << dataSetName << endl;
    
    TFile* inFile = new TFile(inputWTree[iDataSet].c_str(),"READ");
    std::cout  << " inputWTree[iDataSet].c_str() " << inputWTree[iDataSet].c_str() << endl;

    TTree* inWTreeTree = (TTree*) inFile->Get("WTreeTree");
    TBranch* m_br = (TBranch*) inWTreeTree->GetBranch("TheWTree");
    
    WTree* wTree = 0;
    m_br->SetAddress(&wTree);
    
    int nEvent = inWTreeTree->GetEntries();
    
    TTree* inConfigTree = (TTree*) inFile->Get("configTreeWTreeFile");
    TBranch* d_br = (TBranch*) inConfigTree->GetBranch("Dataset");
    TClonesArray* tc_dataset = new TClonesArray("Dataset",0);
    d_br->SetAddress(&tc_dataset);
    
    inConfigTree->GetEvent(0);
    Dataset* dataSet = (Dataset*) tc_dataset->At(0);
    cout << "Processing DataSet " << iDataSet << " : " << dataSetName << "  containing " << nEvent << " events" << endl;

    cout << " ***************************************** " << endl;
    cout << "Before changing --> Cross section = " << dataSet->Xsection() << "  intLumi = " << dataSet->EquivalentLumi() << " Normfactor = " << dataSet->NormFactor() << endl;
    float NominalNormFactor = dataSet->NormFactor();
    if( dataSetName.find("Syst_WJets") == 0 ){
      //dataSet->SetEquivalentLuminosity( dataSet->EquivalentLumi() / (0.7) );  //WJets Minus 30%
      //dataSet->SetEquivalentLuminosity( dataSet->EquivalentLumi() / (0.0000001) );  //WJets Minus 100%
      //dataSet->SetEquivalentLuminosity( dataSet->EquivalentLumi() / (1.3) ); //WJets Plus 30 %
      dataSet->SetEquivalentLuminosity( dataSet->EquivalentLumi() / (2.) ); //WJets Plus 100 %
      //Normfactor value changes without having to change XSection value !!
    }
    cout << "After changing --> Cross section = " << dataSet->Xsection() << "  intLumi = " << dataSet->EquivalentLumi() << " Normfactor = " << dataSet->NormFactor() << endl;
    cout << " ************************************** " << endl;

    // output ascii file stuff
    mkdir("WHelResults_ASCII/",0777);
    string outFileName = "WHelResults_ASCII/WHelResults_" + dataSetName + ".txt";
    ofstream outFile(outFileName.c_str());
    
    outFile << "Output of WHelicities_Analysis.cc" << endl;
    outFile << "First some dataSet info: " << endl;
    outFile << "dataSet Name: " << dataSetName << endl;
    outFile << "dataSet Title: " << dataSet->Title() << endl;
    //outFile << "dataSet cross-section: " << dataSet->Xsection() << endl;
    //outFile << "dataSet integrated lumi: " << dataSet->EquivalentLumi() << endl << endl;
    outFile << "dataSet cross-section: " << XSection << endl;
    outFile << "dataSet integrated lumi: " << EqLumi << endl << endl;    
    outFile << "Start of event-by-event info " << endl << endl;
    
    //Value needed to study the reconstruction efficiency of the leptonic b-jet:
    int CorrectBLeptConstruction=0;
    int ReconstructedEvents=0;

    //Order of indices used in Kinematic Fit:
    int BLeptIndex[12]={3,2,3,1,2,1,3,0,2,0,1,0};
    int BHadrIndex[12]={2,3,1,3,1,2,0,3,0,2,0,1};
    int Quark1Index[12]={0,0,0,0,0,0,1,1,1,1,2,2};
    int Quark2Index[12]={1,1,2,2,3,3,2,2,3,3,3,3};

    outFile << " Kinematic Fit ordering arrays initialized " << endl;

    //Integer needed to represent the first event since iEvt = 0 does not pass the DCoefficient requirement for ttbar Other
    int FirstProcessedEvent=0;

    outFile << " FirstProcessedEvent initialized " << endl;

    vector<float> CosThetaValues[TotalNumberbTags];
    vector<float> CosThetaValuesOrigKins[TotalNumberbTags];
    vector<float> LumiWeightVector[TotalNumberbTags];
    vector<float> LumiWeightVectorOrigKins[TotalNumberbTags];
    int FilledEntries = 0;
    vector<double> CosThGen[TotalNumberbTags];
    vector<double> CosThGenOrigKins[TotalNumberbTags];
    vector<double> EventCorrectionWeight[TotalNumberbTags];
    vector<double> EventCorrWeightOrigKins[TotalNumberbTags];
    float binEdge[CosThetaBinNumber+1];
    float binSize = (1.-(-1.))/15.;
    for(int ii=0; ii<=CosThetaBinNumber;ii++){
      binEdge[ii] = -1 + binSize*ii;
    }

    //Initialize naming of different bTag options:
    int TCHELoop=1;
    int TCHPLoop=1;
    int SSVHELoop=1;
    int SSVHPLoop=1;
    int CSVLoop=1;
       
    if(iDataSet==0){
      while(CSVLoop<=NumberCSVbTags){
	while(SSVHPLoop<=NumberSSVHPbTags){  
	  while(SSVHELoop<=NumberSSVHEbTags){
	    while(TCHPLoop<=NumberTCHPbTags){
	      while(TCHELoop<=NumberTCHEbTags){
		bTagFileOutput[TCHELoop-1+TCHPLoop-1+SSVHELoop-1+SSVHPLoop-1+CSVLoop-1] = bTagName.NameGiving(TCHELoop,NumberTCHEbTags,TCHPLoop,NumberTCHPbTags,SSVHELoop,NumberSSVHEbTags,SSVHPLoop,NumberSSVHPbTags,CSVLoop,NumberCSVbTags);
		PresentationOutput[TCHELoop-1+TCHPLoop-1+SSVHELoop-1+SSVHPLoop-1+CSVLoop-1] = bTagName.NameGivingPres(TCHELoop,NumberTCHEbTags,TCHPLoop,NumberTCHPbTags,SSVHELoop,NumberSSVHEbTags,SSVHPLoop,NumberSSVHPbTags,CSVLoop,NumberCSVbTags);
		TCHELoop++;
		if(TCHELoop == 14){TCHPLoop=2;SSVHELoop=2;SSVHPLoop=2;CSVLoop=2;}
	      }
	      bTagFileOutput[TCHELoop-1+TCHPLoop-1+SSVHELoop-1+SSVHPLoop-1+CSVLoop-1] = bTagName.NameGiving(TCHELoop,NumberTCHEbTags,TCHPLoop,NumberTCHPbTags,SSVHELoop,NumberSSVHEbTags,SSVHPLoop,NumberSSVHPbTags,CSVLoop,NumberCSVbTags);
	      PresentationOutput[TCHELoop-1+TCHPLoop-1+SSVHELoop-1+SSVHPLoop-1+CSVLoop-1] = bTagName.NameGivingPres(TCHELoop,NumberTCHEbTags,TCHPLoop,NumberTCHPbTags,SSVHELoop,NumberSSVHEbTags,SSVHPLoop,NumberSSVHPbTags,CSVLoop,NumberCSVbTags);
	      TCHPLoop++;
	    }
	    bTagFileOutput[TCHELoop-1+TCHPLoop-1+SSVHELoop-1+SSVHPLoop-1+CSVLoop-1] = bTagName.NameGiving(TCHELoop,NumberTCHEbTags,TCHPLoop,NumberTCHPbTags,SSVHELoop,NumberSSVHEbTags,SSVHPLoop,NumberSSVHPbTags,CSVLoop,NumberCSVbTags);
	    PresentationOutput[TCHELoop-1+TCHPLoop-1+SSVHELoop-1+SSVHPLoop-1+CSVLoop-1] = bTagName.NameGivingPres(TCHELoop,NumberTCHEbTags,TCHPLoop,NumberTCHPbTags,SSVHELoop,NumberSSVHEbTags,SSVHPLoop,NumberSSVHPbTags,CSVLoop,NumberCSVbTags);
	    SSVHELoop++;
	  }
	  bTagFileOutput[TCHELoop-1+TCHPLoop-1+SSVHELoop-1+SSVHPLoop-1+CSVLoop-1] = bTagName.NameGiving(TCHELoop,NumberTCHEbTags,TCHPLoop,NumberTCHPbTags,SSVHELoop,NumberSSVHEbTags,SSVHPLoop,NumberSSVHPbTags,CSVLoop,NumberCSVbTags);
	  PresentationOutput[TCHELoop-1+TCHPLoop-1+SSVHELoop-1+SSVHPLoop-1+CSVLoop-1] = bTagName.NameGivingPres(TCHELoop,NumberTCHEbTags,TCHPLoop,NumberTCHPbTags,SSVHELoop,NumberSSVHEbTags,SSVHPLoop,NumberSSVHPbTags,CSVLoop,NumberCSVbTags);
	  SSVHPLoop++;		
	}	
	bTagFileOutput[TCHELoop-1+TCHPLoop-1+SSVHELoop-1+SSVHPLoop-1+CSVLoop-1] = bTagName.NameGiving(TCHELoop,NumberTCHEbTags,TCHPLoop,NumberTCHPbTags,SSVHELoop,NumberSSVHEbTags,SSVHPLoop,NumberSSVHPbTags,CSVLoop,NumberCSVbTags);
	PresentationOutput[TCHELoop-1+TCHPLoop-1+SSVHELoop-1+SSVHPLoop-1+CSVLoop-1] = bTagName.NameGivingPres(TCHELoop,NumberTCHEbTags,TCHPLoop,NumberTCHPbTags,SSVHELoop,NumberSSVHEbTags,SSVHPLoop,NumberSSVHPbTags,CSVLoop,NumberCSVbTags);
	CSVLoop++;
      }
    }
    
    /////////////////////////////////////////
    // Loop on events
    /////////////////////////////////////////
    
    for(unsigned int iEvt=0; iEvt<nEvent; iEvt++){
      //for(unsigned int iEvt=0; iEvt<1000; iEvt++){

      //    for(unsigned int iEvt=0; iEvt<10000; iEvt++){ nEvent = 10000; //nEvent and end of iEvt loop needs to be the same for correctly performing the Minuit Fitter

      inWTreeTree->GetEvent(iEvt);
      if(iEvt%10000 == 0)
        std::cout<<"Processing the "<<iEvt<<"th event, time = "<< ((double)clock() - start) / CLOCKS_PER_SEC <<endl;
      
      // PU reweighting???
      float avPU = ( (float)wTree->nPUBXm1() + (float)wTree->nPU() + (float)wTree->nPUBXp1() ) / 3.; // average in 3 BX!!!, as recommended
      //float lumiWeight = LumiWeights.ITweight( wTree->nPU() );
      
      double lumiWeight3D;
      if(dataSetName.find("Data") != 0){
	lumiWeight3D = Lumi3DWeights.weight3D(wTree->nPUBXm1(),wTree->nPU(),wTree->nPUBXp1());
      }

      //if(dataSetName.find("Data") == 0) lumiWeight = 1;
      if(dataSetName.find("Data") == 0) lumiWeight3D = 1;
      //if( dataSetName.find("Fall10") != string::npos ) lumiWeight = 1; //no PU in Fall10!
      //histo1D["lumiWeights"]->Fill(lumiWeight);
      histo1D["lumiWeights3D"]->Fill(lumiWeight3D);
      
      // scale factor for the event
      float scaleFactor = 1.; 

      //Primary vertices:
      float nPrimaryVertices = wTree->nPV();
          
      //Different bTag values:
      vector<float> btagSSVHE = wTree->bTagSSVHE();
      vector<float> btagSSVHP = wTree->bTagSSVHP();
      vector<float> btagTCHE = wTree->bTagTCHE();
      vector<float> btagTCHP = wTree->bTagTCHP();
      vector<float> btagCSV = wTree->bTagCSV();
      
      //Generator information:
      int CorrectQuark1 = wTree->hadrLJet1();
      int CorrectQuark2 = wTree->hadrLJet2();
      int CorrectBHadr = wTree->hadrBJet();
      int CorrectBLept = wTree->leptBJet();
      
      //--> Use this to obtain the correct Kinematic Fit index:
      int CorrectKinFitIndex=9999;
      for(int ii=0; ii<12; ii++){
	if( ( CorrectQuark1 == Quark1Index[ii] && CorrectQuark2 == Quark2Index[ii] && CorrectBHadr == BHadrIndex[ii] ) || ( CorrectQuark1 == Quark2Index[ii] && CorrectQuark2 == Quark1Index[ii] && CorrectBHadr == BHadrIndex[ii] ) ){
	  CorrectKinFitIndex = ii;
	}
      }

      //Generator particles:
      TLorentzVector genNeutrino = wTree->standardNeutrino();
      TLorentzVector genMuon = wTree->standardLepton();     
      //TLorentzVector hadrBQuark = wTree->hadrBQuark();
      //TLorentzVector hadrLQuark1 = wTree->hadrLQuark1();
      //TLorentzVector hadrLQuark2 = wTree->hadrLQuark2();
      //TLorentzVector leptBQuark = wTree->leptBQuark();
      
      //Cos theta value on generator level:
      float CosThetaGenerator = wTree->standardCosTheta();
      
      // ChiSquared values and corresponding Leptonic b-jet index from Kinematic Fit (on hadronic side only)
      //      vector<float> ChiSquaredValue; 
      
      /////////////////////////////////////////////////////////////////////////////
      //Read in Chi squared and particles after Hadronic and Leptonic KinFit:
      /////////////////////////////////////////////////////////////////////////////
      
      bool UseChangedKinematics = true;  //Boolean to differentiate between changed kinematics and original kinematics of fitted particles
      bool LeptTopMassConstrained = true;  //Boolean to differentiate between lept top mass constrained to world average and lept tom mass left free in KinFit
      bool LeptonicFit = true; //Boolean to differentiate between KinFit on hadronic side only and KinFit on hadronic+leptonic side

      vector<float> ChiSqKinFit;
      
      vector<TLorentzVector> selectedJets = wTree->selectedJets();
      TLorentzVector muon = wTree->muon(); //Needed for offline muon cut!  
      TLorentzVector MET = wTree->met();
      
      vector<TLorentzVector> leptBJetKinFit;
      vector<TLorentzVector> muonKinFit;
      vector<TLorentzVector> neutrinoKinFit;
      
      //Use Original kinematics together with changed kinematics to understand the origin of the data-mc discrepancy seen in cos theta* distribution
      vector<TLorentzVector> leptBJetOrig;
      vector<TLorentzVector> muonOrig;
      vector<TLorentzVector> neutrinoOrig;
      vector<float> ChiSqKinFitOrig;
      for(unsigned int iCombi=0; iCombi<12; iCombi++){      	
	ChiSqKinFitOrig.push_back(wTree->chi2KinFit(iCombi));  //Chi squared obtained by performing a KinFit on hadronic side of the event only. Use original kinematics to reconstruct the event to avoid too much influence from the Kinematic Fit.
      }

      //--------------------------------
      //  Use original kinematics : 
      //--------------------------------
      if(UseChangedKinematics == false){
	//ooooooOOOOOOOOOOOooooooooooOOOOOOOOOoooooooo
	// Apply KinFit on hadronic + leptonic side: 
	//ooooooOOOOOOOOOOOooooooooooOOOOOOOOOoooooooo
	if(LeptonicFit == true){
	  if(iEvt == 0) std::cout << " -- Performing analysis with original kinematics before Hadronic + Leptonic KinFit " << endl;
	  //selectedJets = wTree->selectedJets();
	  //muon = wTree->muon();
	  //MET = wTree->met();
	  
	  if(LeptTopMassConstrained == true){
	    if(iEvt == 0) std::cout << "   --  Performing analysis with original kinematics before KinFit on hadronic + leptonic side with theoretical mass constraint on leptonic top !!!! " << std::endl;
	    for(unsigned int iCombi=0; iCombi<12; iCombi++){      	
	      ChiSqKinFit.push_back(wTree->chi2FullKinFit(iCombi));   
	    }
	  }
	  else{
	    if(iEvt == 0) std::cout << "   --  Performing analysis with original kinematics before KinFit on hadronic + leptonic side with leptonic top mass left free !!!! " << std::endl;
	    for(unsigned int iCombi=0; iCombi<12; iCombi++){      	
	      ChiSqKinFit.push_back(wTree->chi2FullKinFitMassFit(iCombi));   
	    }
	  }
	}
	//ooooooOOOOOOOOOOOooooooooooOOOOOOOOOOO
	// Apply KinFit on hadronic side only: 
	//ooooooOOOOOOOOOOOooooooooooOOOOOOOOOOO
	else if(LeptonicFit == false){
	  if(iEvt == 0) std::cout << " -- Performing analysis with original kinematics before Hadronic KinFit " << endl;
	  //selectedJets = wTree->selectedJets();
	  //muon = wTree->muon();
	  //MET = wTree->met();
	  
	  if(iEvt == 0) std::cout << "   --  Performing analysis with original kinematics before KinFit on hadronic side with theoretical mass constraint !!!! " << std::endl;
	  for(unsigned int iCombi=0; iCombi<12; iCombi++){      	
	    ChiSqKinFit.push_back(wTree->chi2KinFit(iCombi));   
	  }	  	  
	}
      }
      //--------------------------------
      //  Use changed kinematics : 
      //--------------------------------
      else if(UseChangedKinematics == true){   
	//ooooooOOOOOOOOOOOooooooooooOOOOOOOOOoooooooo
	// Apply KinFit on hadronic + leptonic side: 
	//ooooooOOOOOOOOOOOooooooooooOOOOOOOOOoooooooo
	if(LeptonicFit == true){
	  if(LeptTopMassConstrained == true){
	    if(iEvt == 0) std::cout << "   --  Performing analysis with kinematics changed after KinFit on hadronic + leptonic side with theoretical mass constraint on leptonic top !!!! " << std::endl;
	    for(unsigned int iCombi=0; iCombi<12; iCombi++){      	
	      ChiSqKinFit.push_back(wTree->chi2FullKinFit(iCombi));   
	      leptBJetKinFit.push_back(wTree->fittedFullLeptB(iCombi));
	      muonKinFit.push_back(wTree->fittedFullLepton(iCombi));
	      neutrinoKinFit.push_back(wTree->fittedFullNeutrino(iCombi));   
	    }
	  }
	  else{
	    if(iEvt == 0) std::cout << "   --  Performing analysis with kinematics changed after KinFit on hadronic + leptonic side with leptonic top mass left free !!!! " << std::endl;
	    for(unsigned int iCombi=0; iCombi<12; iCombi++){      		    
	      ChiSqKinFit.push_back(wTree->chi2FullKinFitMassFit(iCombi));   
	      leptBJetKinFit.push_back(wTree->fittedFullLeptBMassFit(iCombi));
	      muonKinFit.push_back(wTree->fittedFullLeptonMassFit(iCombi));
	      neutrinoKinFit.push_back(wTree->fittedFullNeutrinoMassFit(iCombi));   
	    }	  
	  }	
	}
	//ooooooOOOOOOOOOOOooooooooooOOOOOOOOOOO
	// Apply KinFit on hadronic side only: 
	//ooooooOOOOOOOOOOOooooooooooOOOOOOOOOOO	
	else if(LeptonicFit == false){
	  if(LeptTopMassConstrained == true){
	    if(iEvt == 0) std::cout << "   --  Performing analysis with kinematics changed after KinFit on hadronic side with theoretical mass constraint !!!! " << std::endl;
	    for(unsigned int iCombi=0; iCombi<12; iCombi++){      	
	      ChiSqKinFit.push_back(wTree->chi2KinFit(iCombi));   
	      leptBJetKinFit.push_back(wTree->fittedLeptB(iCombi));
	      muonKinFit.push_back(wTree->fittedLepton(iCombi));
	      neutrinoKinFit.push_back(wTree->fittedNeutrino(iCombi));   
	    }
	  }
	}
      }
      
      //ooooooooOOOOOOOOOOOOooooooooooooOOOOOOOOOOOOooooooooooooOOOOO
      //ooOOooOOoo      Reading out nTuples done           ooOOooOOoo
      //ooOOooOOoo-----------------------------------------ooOOooOOoo
      //ooOOooOOoo      Start of actual analysis           ooOOooOOoo
      //ooooooooOOOOOOOOOOOOooooooooooooOOOOOOOOOOOOooooooooooooOOOOO
      float TransverseMass = sqrt(2*(abs(muon.Pt()))*abs(MET.Pt())*(1-cos(muon.DeltaPhi(MET))));	

      //----------------------------------
      //     Require some extra cuts:
      //----------------------------------
      bool eventSelected = false;

      if(dataSetName.find("TTbarJets_SemiMu") ==0) NumberEventsBeforeCuts++;
      if(dataSetName.find("Data") ==0) NumberDataEventsBeforeCuts++;
      
      MSPlot["nPVBeforeCuts"]->Fill(nPrimaryVertices,datasets[iDataSet], true, Luminosity*scaleFactor*lumiWeight3D);
      selecTableMacro.Fill(iDataSet,0,scaleFactor*lumiWeight3D);

      //Medium SSVHE bTag:
      if( btagSSVHE[0] > 1.74 || btagSSVHE[1] > 1.74 || btagSSVHE[2] > 1.74 || btagSSVHE[3] > 1.74){
	selecTableMacro.Fill(iDataSet,1,scaleFactor*lumiWeight3D);

	if(dataSetName.find("TTbarJets_SemiMu") ==0) NumberEventsAfterbTag++;
	
	MSPlot["nPVAfterSSVHEMbTag"]->Fill(nPrimaryVertices,datasets[iDataSet], true, Luminosity*scaleFactor*lumiWeight3D);

	//Require offline muon cut of 27 (to avoid turn-on of IsoMu17/20/24 triggers)  -- Use 25 for CIEMAT comparison (value applied in tree)!
	//No extra offline muon cut for IsoMu17_TriCentralJet30 trigger --> Cut of 20 GeV is already applied
	//if(muon.Pt() >=27){
	selecTableMacro.Fill(iDataSet,2,scaleFactor*lumiWeight3D);
	
	MSPlot["nPVAfterMuon27Cut"]->Fill(nPrimaryVertices,datasets[iDataSet], true, Luminosity*scaleFactor*lumiWeight3D);
	
	MSPlot["TransverseMassBeforeCut"]->Fill(TransverseMass, datasets[iDataSet], true, Luminosity);	  
	if(TransverseMass > 30){
	  selecTableMacro.Fill(iDataSet,3,scaleFactor*lumiWeight3D);

	  if(dataSetName.find("TTbarJets_SemiMu") ==0) NumberEventsAfterTransverseMass++;

	  MSPlot["TransverseMassAfterCut"]->Fill(TransverseMass, datasets[iDataSet], true, Luminosity);	      	  
	  MSPlot["nPVAfterTransverseMassCut"]->Fill(nPrimaryVertices,datasets[iDataSet], true, Luminosity*scaleFactor*lumiWeight3D);
          
	  eventSelected = true;
	}
      }
      //}
      
      if(eventSelected == true){

	if(dataSetName.find("TTbarJets_SemiMu") ==0) NumberSelectedEvents++;
	if(dataSetName.find("Data") ==0) NumberSelectedDataEvents++;
	
	MSPlot["nPrimaryVert"]->Fill(nPrimaryVertices,datasets[iDataSet], true, Luminosity*scaleFactor*lumiWeight3D);    
	
	MSPlot["Jet1Pt"]->Fill(selectedJets[0].Pt(), datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D);    
	MSPlot["Jet2Pt"]->Fill(selectedJets[1].Pt(), datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D); 
	MSPlot["Jet3Pt"]->Fill(selectedJets[2].Pt(), datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D); 
	MSPlot["Jet4Pt"]->Fill(selectedJets[3].Pt(), datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D); 	  

	//-----------------------------------------------------
	//    Reconstruction of correct jet distribution
	//-----------------------------------------------------
	  
	//Obtain probabilities:
	vector<float> ProbabilityOfKinFit;	 
	vector<float> KinFitProbOrigKins;
	vector<float> ProbSOverSB;                 //Need to change WTree classes such that Mlb method is removed !!!!
	for(int jj = 0; jj < 12 ; jj++){ 
	  if(ChiSqKinFit[jj]!=9999){ ProbabilityOfKinFit.push_back(TMath::Prob(ChiSqKinFit[jj],2));}
	  else{ProbabilityOfKinFit.push_back(-1.);}

	  if(ChiSqKinFitOrig[jj] != 9999){ KinFitProbOrigKins.push_back(TMath::Prob(ChiSqKinFitOrig[jj],2));}
	  else{KinFitProbOrigKins.push_back(-1.);}

	  ProbSOverSB.push_back( 1.);  	  
	}	
			
	 MSPlot["MetPhi"]->Fill(MET.Phi(),datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D);  
	 //MSPlot["NeutrinoKinFitPhi"]->Fill(neutrinoKinFit[HighestProbComb].Phi(),datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D);  
	// //MSPlot["NeutrinoPhiDiff"]->Fill((genNeutrino.Phi()-neutrinoKinFit[HighestProbComb].Phi()),datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D);

	 MSPlot["MetPx"]->Fill(MET.Px(),datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D);         
	 //MSPlot["NeutrinoKinFitPx"]->Fill(neutrinoKinFit[HighestProbComb].Px(),datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D);  
	// //MSPlot["NeutrinoPxDiff"]->Fill((genNeutrino.Px()-neutrinoKinFit[HighestProbComb].Px()),datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D); 

	 MSPlot["MetPy"]->Fill(MET.Py(),datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D);  
	// MSPlot["NeutrinoKinFitPy"]->Fill(neutrinoKinFit[HighestProbComb].Py(),datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D);  
	// //MSPlot["NeutrinoPyDiff"]->Fill((genNeutrino.Py()-neutrinoKinFit[HighestProbComb].Py()),datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D);
 
	// MSPlot["NeutrinoKinFitPz"]->Fill(neutrinoKinFit[HighestProbComb].Pz(),datasets[iDataSet],true, Luminosity*scaleFactor*lumiWeight3D);  

	//-------------------------------------------------------------
	//Obtain jet combination for the different b-tag constraints:
	//------------------------------------------------------------      		
	
	//Initialize bTag loop variables:
	int TCHEbTagLoop=1;
	int TCHPbTagLoop=1; 
	int SSVHEbTagLoop=1;
	int SSVHPbTagLoop=1;
	int CSVbTagLoop=1;
      
	int ConsideredBTagger; //0=tche, 1 = tchp, 2 = ssvhe, 3 = ssvhp & 4 = csv
            
	while(CSVbTagLoop <= NumberCSVbTags){
	  while(SSVHPbTagLoop <=NumberSSVHPbTags ){
	    while(SSVHEbTagLoop <=NumberSSVHEbTags ){
	      while(TCHPbTagLoop <= NumberTCHPbTags){
		while(TCHEbTagLoop <= NumberTCHEbTags){	   
		  ConsideredBTagger=0;
		  
		  int JetCombination = bTagJetSelection.HighestProbSelection(TCHEbTagLoop,ConsideredBTagger,ProbabilityOfKinFit,ProbSOverSB,btagTCHE,btagTCHP,btagSSVHE,btagSSVHP,btagCSV);

		  if(TCHEbTagLoop == 1){
		    MSPlot["nPVBeforeFoundJetComb"]->Fill(nPrimaryVertices,datasets[iDataSet], true, Luminosity*scaleFactor*lumiWeight3D);
		  }
		  
		  int BLeptonicIndex = BLeptIndex[JetCombination];
		  int BHadronicIndex = BHadrIndex[JetCombination];
		    
		  if(JetCombination != 999){//&& JetCombination == CorrectKinFitIndex){
		    
		    if(TCHEbTagLoop == 1){MSPlot["nPVAfterFoundJetComb"]->Fill(nPrimaryVertices,datasets[iDataSet], true, Luminosity*scaleFactor*lumiWeight3D);}
		    if(TCHEbTagLoop == 7){MSPlot["nPVAfterFoundJetCombbTag"]->Fill(nPrimaryVertices,datasets[iDataSet], true, Luminosity*scaleFactor*lumiWeight3D);}
		    
		    float CosThetaCalculation;
		    if(UseChangedKinematics == true) CosThetaCalculation = bTagCosThetaCalculation.Calculation(muonKinFit[JetCombination],neutrinoKinFit[JetCombination],leptBJetKinFit[JetCombination]);
		    else if(UseChangedKinematics == false) CosThetaCalculation = bTagCosThetaCalculation.CalcOrigKins(BLeptonicIndex,BHadronicIndex,muon,selectedJets,MassW, MassTop);
		
		    if(CosThetaCalculation != 999){	
		      
		      if(TCHEbTagLoop == 1){
			MSPlot["nPVAfterFoundCosTheta"]->Fill(nPrimaryVertices,datasets[iDataSet], true, Luminosity*scaleFactor*lumiWeight3D);

			if(dataSetName.find("TTbarJets_SemiMu") ==0){
			  NumberUsedEvents++;
			  if(BLeptonicIndex == CorrectBLept) NumberUsedCorrectEvents++;
			  else if(BLeptonicIndex != CorrectBLept) NumberUsedWrongEvents++;
			}
			if(dataSetName.find("Data") ==0) NumberUsedDataEvents++;
		      }
		      if(TCHEbTagLoop == 7){MSPlot["nPVAfterFoundCosThetabTag"]->Fill(nPrimaryVertices,datasets[iDataSet], true, Luminosity*scaleFactor*lumiWeight3D);}
		      
		      if(ProbabilityOfKinFit[JetCombination] >= KinFitCut){
		      
			NumberRemainingEvents[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1][iDataSet]++;
			CosThetaValues[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(CosThetaCalculation);
			LumiWeightVector[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(lumiWeight3D);
			if(dataSetName.find("TTbarJets_SemiMu") ==0 && dataSetName.find("JES") != 0){
			  if(BLeptonicIndex == CorrectBLept){//Count the number of events with correctly reconstructed leptonic b-jet
			    NumberBLeptCorrectEvents[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1][iDataSet]++;
			  }
			  CosThGen[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(CosThetaGenerator);
			  EventCorrectionWeight[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(scaleFactor*Luminosity*lumiWeight3D*dataSet->NormFactor());		      		      
			}//End of semiMu sample
		      }
		      
		      if(TCHEbTagLoop == 7){ //optimal bTag case
			MSPlot["CosThetaTCHEMLept"]->Fill(CosThetaCalculation,datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			MSPlot["KinFitProbTCHEMLept"]->Fill(ProbabilityOfKinFit[JetCombination],datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);	
		      }		
		      
		      if(TCHEbTagLoop == 1){ //No bTag case
			if(UseChangedKinematics == true){
			  MSPlot["BLeptPtAfterKinFit"]->Fill(leptBJetKinFit[JetCombination].Pt(),datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			  MSPlot["MetPtAfterKinFit"]->Fill(neutrinoKinFit[JetCombination].Pt(),datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			  MSPlot["MuonPtAfterKinFit"]->Fill(muonKinFit[JetCombination].Pt(),datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			  MSPlot["WLeptPtAfterKinFit"]->Fill((neutrinoKinFit[JetCombination]+muonKinFit[JetCombination]).Pt(),datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			  MSPlot["TopLeptPtAfterKinFit"]->Fill((neutrinoKinFit[JetCombination]+muonKinFit[JetCombination]+leptBJetKinFit[JetCombination]).Pt(),datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			}
			else{
			  MSPlot["BLeptPtAfterKinFit"]->Fill((selectedJets[BLeptonicIndex]).Pt(),datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			  MSPlot["MetPtAfterKinFit"]->Fill(MET.Pt(),datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			  MSPlot["MuonPtAfterKinFit"]->Fill(muon.Pt(),datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			  MSPlot["WLeptPtAfterKinFit"]->Fill((MET+muon).Pt(),datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			  MSPlot["TopLeptPtAfterKinFit"]->Fill((selectedJets[BLeptonicIndex]+MET+muon).Pt(),datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			}
			MSPlot["CosThetaNobTag"]->Fill(CosThetaCalculation,datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
			MSPlot["KinFitProbNobTag"]->Fill(ProbabilityOfKinFit[JetCombination],datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);

			if(dataSetName.find("TTbarJets_SemiMu") == 0){
			  if(BLeptonicIndex == CorrectBLept){
			    histo1D["CosThetaResNobTag"]->Fill(CosThetaCalculation - CosThetaGenerator);
			    histo1D["KinFitProbCorrectBLept"]->Fill(ProbabilityOfKinFit[JetCombination]);
			    histo1D["CosThetaCorrectBLept"]->Fill(CosThetaCalculation);
			  }
			  else if(BLeptonicIndex != CorrectBLept){
			    histo1D["KinFitProbWrongBLept"]->Fill(ProbabilityOfKinFit[JetCombination]);
			    histo1D["CosThetaWrongBLeptSSVHEM"]->Fill(CosThetaCalculation);
			  }
			}
		      }
		      
		    }
		  }	      

		  TCHEbTagLoop++;
		  if(TCHEbTagLoop == 14){
		    TCHPbTagLoop=2;
		    SSVHEbTagLoop=2;
		    SSVHPbTagLoop=2;
		    CSVbTagLoop=2;
		  }
		}
		ConsideredBTagger=1;
	      
		int JetCombination = bTagJetSelection.HighestProbSelection(TCHPbTagLoop,ConsideredBTagger,ProbabilityOfKinFit,ProbSOverSB,btagTCHE,btagTCHP,btagSSVHE,btagSSVHP,btagCSV);
		  
		int BLeptonicIndex = BLeptIndex[JetCombination];
		int BHadronicIndex = BHadrIndex[JetCombination];

		if(JetCombination != 999){//&& JetCombination == CorrectKinFitIndex){
		  float CosThetaCalculation;
		  if(UseChangedKinematics == true) CosThetaCalculation = bTagCosThetaCalculation.Calculation(muonKinFit[JetCombination],neutrinoKinFit[JetCombination],leptBJetKinFit[JetCombination]);
		  else if(UseChangedKinematics == false) CosThetaCalculation = bTagCosThetaCalculation.CalcOrigKins(BLeptonicIndex,BHadronicIndex,muon,selectedJets,MassW, MassTop);
		  
		  if(CosThetaCalculation != 999 && ProbabilityOfKinFit[JetCombination] >= KinFitCut){	

		    if(TCHPbTagLoop == 7){
		      MSPlot["CosThetaTCHPMLept"]->Fill(CosThetaCalculation,datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
		    }
	    
		    NumberRemainingEvents[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1][iDataSet]++;
		    CosThetaValues[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(CosThetaCalculation);
		    LumiWeightVector[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(lumiWeight3D);
		    if(dataSetName.find("TTbarJets_SemiMu") ==0 && dataSetName.find("JES") != 0){
		      if(BLeptonicIndex == CorrectBLept){//Count the number of events with correctly reconstructed leptonic b-jet
			NumberBLeptCorrectEvents[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1][iDataSet]++;
		      }
		      CosThGen[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(CosThetaGenerator);
		      EventCorrectionWeight[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(scaleFactor*Luminosity*lumiWeight3D*dataSet->NormFactor());		      		      
		    }//End of semiMu sample
		  }
		}
				
		TCHPbTagLoop++;
	      }
	      ConsideredBTagger=2;

	      int JetCombination = bTagJetSelection.HighestProbSelection(SSVHEbTagLoop,ConsideredBTagger,ProbabilityOfKinFit,ProbSOverSB,btagTCHE,btagTCHP,btagSSVHE,btagSSVHP,btagCSV);
		  
	      int BLeptonicIndex = BLeptIndex[JetCombination];
	      int BHadronicIndex = BHadrIndex[JetCombination];
		    
	      if(JetCombination != 999){//&& JetCombination == CorrectKinFitIndex){
		float CosThetaCalculation;
		if(UseChangedKinematics == true) CosThetaCalculation = bTagCosThetaCalculation.Calculation(muonKinFit[JetCombination],neutrinoKinFit[JetCombination],leptBJetKinFit[JetCombination]);
		else if(UseChangedKinematics == false) CosThetaCalculation = bTagCosThetaCalculation.CalcOrigKins(BLeptonicIndex,BHadronicIndex,muon,selectedJets,MassW, MassTop);
		
		if(CosThetaCalculation != 999 && ProbabilityOfKinFit[JetCombination] >= KinFitCut){

		  if(SSVHEbTagLoop == 7){
		    MSPlot["CosThetaSSVHEMLept"]->Fill(CosThetaCalculation,datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
		    MSPlot["KinFitProbSSVHEMLept"]->Fill(ProbabilityOfKinFit[JetCombination],datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);

		    if(dataSetName.find("TTbarJets_SemiMu") ==0 && dataSetName.find("JES") != 0){
		      histo1D["CosThetaResSSVHEMLept"]->Fill(CosThetaCalculation - CosThetaGenerator);
		      if(BLeptonicIndex == CorrectBLept){
			histo1D["KinFitProbCorrectBLeptSSVHEM"]->Fill(ProbabilityOfKinFit[JetCombination]);
			histo1D["CosThetaCorrectBLeptSSVHEM"]->Fill(CosThetaCalculation);
		      }
		      else if(BLeptonicIndex != CorrectBLept){
			histo1D["KinFitProbWrongBLeptSSVHEM"]->Fill(ProbabilityOfKinFit[JetCombination]);
			histo1D["CosThetaWrongBLeptSSVHEM"]->Fill(CosThetaCalculation);
		      }

		    }
		  }

		  NumberRemainingEvents[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1][iDataSet]++;
		  CosThetaValues[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(CosThetaCalculation);
		  LumiWeightVector[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(lumiWeight3D);
		  if(dataSetName.find("TTbarJets_SemiMu") ==0 && dataSetName.find("JES") != 0){
		    if(BLeptonicIndex == CorrectBLept){//Count the number of events with correctly reconstructed leptonic b-jet
		      NumberBLeptCorrectEvents[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1][iDataSet]++;
		    }
		    CosThGen[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(CosThetaGenerator);
		    EventCorrectionWeight[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(scaleFactor*Luminosity*lumiWeight3D*dataSet->NormFactor()); 
		  }//End of semiMu sample
		  
		}
	      }	  
	      SSVHEbTagLoop++;
	    }
	    ConsideredBTagger=3;

	    int JetCombination = bTagJetSelection.HighestProbSelection(SSVHPbTagLoop,ConsideredBTagger,ProbabilityOfKinFit,ProbSOverSB,btagTCHE,btagTCHP,btagSSVHE,btagSSVHP,btagCSV);
		  
	    int BLeptonicIndex = BLeptIndex[JetCombination];
	    int BHadronicIndex = BHadrIndex[JetCombination];
		    
	    if(JetCombination != 999){//&& JetCombination == CorrectKinFitIndex){		
	      float CosThetaCalculation;
	      if(UseChangedKinematics == true) CosThetaCalculation = bTagCosThetaCalculation.Calculation(muonKinFit[JetCombination],neutrinoKinFit[JetCombination],leptBJetKinFit[JetCombination]);	      
	      else if(UseChangedKinematics == false) CosThetaCalculation = bTagCosThetaCalculation.CalcOrigKins(BLeptonicIndex,BHadronicIndex,muon,selectedJets,MassW, MassTop);

	      if(CosThetaCalculation != 999 && ProbabilityOfKinFit[JetCombination] >= KinFitCut){

		if(SSVHPbTagLoop == 7){
		  MSPlot["CosThetaSSVHPMLept"]->Fill(CosThetaCalculation,datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
		  MSPlot["KinFitProbSSVHPMLept"]->Fill(ProbabilityOfKinFit[JetCombination],datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
		}
		    
		NumberRemainingEvents[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1][iDataSet]++;
		CosThetaValues[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(CosThetaCalculation);
		LumiWeightVector[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(lumiWeight3D);
		if(dataSetName.find("TTbarJets_SemiMu") ==0 && dataSetName.find("JES") != 0){
		  if(BLeptonicIndex == CorrectBLept){//Count the number of events with correctly reconstructed leptonic b-jet
		    NumberBLeptCorrectEvents[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1][iDataSet]++;
		  }
		  CosThGen[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(CosThetaGenerator);
		  EventCorrectionWeight[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(scaleFactor*Luminosity*lumiWeight3D*dataSet->NormFactor());    
		}//End of semiMu sample	      
	      }	
	    }	
	    SSVHPbTagLoop++;
	  }
	  ConsideredBTagger=4;

	  int JetCombination = bTagJetSelection.HighestProbSelection(CSVbTagLoop,ConsideredBTagger,ProbabilityOfKinFit,ProbSOverSB,btagTCHE,btagTCHP,btagSSVHE,btagSSVHP,btagCSV);
		  
	  int BLeptonicIndex = BLeptIndex[JetCombination];
	  int BHadronicIndex = BHadrIndex[JetCombination];
		    
	  if(JetCombination != 999 ){//&& JetCombination == CorrectKinFitIndex){
	    float CosThetaCalculation;
	    if(UseChangedKinematics == true) CosThetaCalculation = bTagCosThetaCalculation.Calculation(muonKinFit[JetCombination],neutrinoKinFit[JetCombination],leptBJetKinFit[JetCombination]);
	    else if(UseChangedKinematics == false) CosThetaCalculation = bTagCosThetaCalculation.CalcOrigKins(BLeptonicIndex,BHadronicIndex,muon,selectedJets,MassW, MassTop);
	    
	    if(CosThetaCalculation != 999 && ProbabilityOfKinFit[JetCombination] >= KinFitCut){

	      
	      if(CSVbTagLoop == 7){
		MSPlot["CosThetaCSVMLept"]->Fill(CosThetaCalculation,datasets[iDataSet],true,Luminosity*scaleFactor*lumiWeight3D);
	      }
		    
	      NumberRemainingEvents[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1][iDataSet]++;
	      CosThetaValues[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(CosThetaCalculation);
	      LumiWeightVector[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(lumiWeight3D);
	      if(dataSetName.find("TTbarJets_SemiMu") ==0 && dataSetName.find("JES") != 0){
		if(BLeptonicIndex == CorrectBLept){//Count the number of events with correctly reconstructed leptonic b-jet
		  NumberBLeptCorrectEvents[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1][iDataSet]++;
		}
		CosThGen[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(CosThetaGenerator);
		EventCorrectionWeight[TCHEbTagLoop-1+TCHPbTagLoop-1+SSVHEbTagLoop-1+SSVHPbTagLoop-1+CSVbTagLoop-1].push_back(scaleFactor*Luminosity*lumiWeight3D*dataSet->NormFactor());     
	      }//End of semiMu sample	    
	    }	
	  }	
	  CSVbTagLoop++;
	}
		
      }// End of loop over selected events
      
    } // end loop over events in wTrees    

    std::cout << "  " << endl;
    std::cout << " size of cos theta : " << CosThetaValues[0].size() << endl;
    std::cout << " °°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°°° " << endl;

    
    int TCHE=0;
    int TCHP=0;
    int SSVHE=0;
    int SSVHP=0;
    int CSV=0;
    
    int ndimen=3;
    
    while(CSV<13){
      while(SSVHP<13){ //Try to improve code by making a Minuit fitter class !!
	while(SSVHE<13){
	  while(TCHP<13){
	    while(TCHE<13){
	      
	      std::string CosThetaDataString = "CosThetaData_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	      std::string CosThetaSignalString = "CosThetaSignal_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	      std::string CosThetaBckgString = "CosThetaBckg_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	      std::string CosThetaGeneratorString = "CosThetaGenerator_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	      
	      if(iDataSet == 0){
		histo1D[CosThetaDataString]=new TH1F(CosThetaDataString.c_str(),CosThetaDataString.c_str(),CosThetaBinNumber,-1,1);
		histo1D[CosThetaDataString]->SetDirectory(0);
		histo1D[CosThetaSignalString]=new TH1F(CosThetaSignalString.c_str(),CosThetaSignalString.c_str(),CosThetaBinNumber,-1,1);
		histo1D[CosThetaSignalString]->SetDirectory(0);
		histo1D[CosThetaBckgString]=new TH1F(CosThetaBckgString.c_str(),CosThetaBckgString.c_str(),CosThetaBinNumber,-1,1);
		histo1D[CosThetaBckgString]->SetDirectory(0);
	      }
	      
	      if(dataSetName.find("TTbarJets_SemiMu")==0 ){   //Filling of the genttbar histo
		char hisname[100];
		sprintf(hisname,"CosThetaGenerator_TCHE%s_TCHP%s_SSVHE%s_SSVHP%s_CSV%s", bTagValues[TCHE].c_str(),bTagValues[TCHP].c_str(),bTagValues[SSVHE].c_str(),bTagValues[SSVHP].c_str(),bTagValues[CSV].c_str());
		for (int ibinn=0; ibinn<CosThetaBinNumber; ibinn++){
		  genttbarhisto[ibinn]= new TNtuple(hisname,hisname,"costhgen:evtweight");
		  genttbarhisto[ibinn]->SetDirectory(0);
		}
		for(int ii=0; ii<CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV].size();ii++){		
		  for(int iBin=0; iBin< CosThetaBinNumber; iBin++){
		    
		    if(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] >= binEdge[iBin] && CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] < binEdge[iBin+1]){
		      double costhgood = CosThGen[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]; 
		      double thisWeight = EventCorrectionWeight[TCHE+TCHP+SSVHE+SSVHP+CSV][ii];
		      genttbarhisto[iBin]->Fill(costhgood , thisWeight) ; 
		    }
		    else if(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] ==1){ //1 is included in last bin
		      genttbarhisto[CosThetaBinNumber-1]->Fill(CosThGen[TCHE+TCHP+SSVHE+SSVHP+CSV][ii], EventCorrectionWeight[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]);
		    }	      	   
		  }
		}
	      }
	      
	      float scaleFactor = 1.;  //Make an array of the scaleFactor such that it can be different for every event !!
	      
	      for(int ii=0;ii<CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV].size();ii++){
		//Change data to systematics since then nominal values will be reweighted!!
		//if(dataSetName.find("WJets_Nominal") != 0)  //WJets syst
		if(dataSetName.find("Data") == 0 || dataSetName.find("JES") == 0) //Nominal and JES systematics
		  histo1D[CosThetaDataString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],scaleFactor*(LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii])*Luminosity*dataSet->NormFactor());    				
		
		if(dataSetName.find("TTbarJets_SemiMu") == 0 && dataSetName.find("JES_") != 0)
		  histo1D[CosThetaSignalString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],Luminosity*scaleFactor*(LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii])*NominalNormFactor);
		else if(dataSetName.find("Data") !=0 && dataSetName.find("JES") != 0 && dataSetName.find("Syst") != 0) 
		  histo1D[CosThetaBckgString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],Luminosity*scaleFactor*(LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii])*NominalNormFactor);
	      }
	      
	      if(iDataSet==(datasets.size()-1)){//Go in this loop when the last datasample is active
		
		MyChi2 myChi2 (histo1D[CosThetaDataString], histo1D[CosThetaSignalString], histo1D[CosThetaBckgString], 0.6671, 0.3325, 0.0004); 		

		TMinuitMinimizer minimizer;
		minimizer.SetFunction(myChi2);
		minimizer.SetErrorDef(1.0);   // 1.0 for chi2, 0.5 for -logL
		minimizer.SetVariable(0, "F0", 0.6671, 1.e-4);
		minimizer.SetVariable(1, "FL", 0.3325, 1.e-4);
		if (ndimen==3)  minimizer.SetVariable(2, "Normal", 1., 1.e-4); 

		bool isValid = minimizer.Minimize();
		double f0result, frresult,flresult;
		double ef0result, efrresult,eflresult;
		if (isValid) {
		  //minimizer.PrintResults();
		  f0result =minimizer.X()[0]; ef0result= minimizer.Errors()[0];
		  flresult= minimizer.X()[1]; eflresult= minimizer.Errors()[1];
		  
		  PresentationTex << PresentationOutput[TCHE+TCHP+SSVHE+SSVHP+CSV] << " & ";
		  for(int ii=0; ii< nameDataSet.size(); ii++){
		    if(nameDataSet[ii].find("JES") != 0 && nameDataSet[ii].find("Syst") != 0 && ii < nameDataSet.size()-1){ //Only output for samples with no systematics
		      PresentationTex << NumberRemainingEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & ";
		    }
		    else if(ii == nameDataSet.size()-1 ){ 
		      PresentationTex << NumberRemainingEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & "; //For presentation this is not the end of the table, helicity values still need to be included !!
		      PresentationTex << NumberBLeptCorrectEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & ";
		    }
		  }		  
		  if (ndimen==3){
		    frresult = 1.-minimizer.X()[0]-minimizer.X()[1];
		    double er0=minimizer.Errors()[0];
		    double er1=minimizer.Errors()[1];
		    double cov01 = minimizer.CovMatrix(0,1);		  
		    efrresult = sqrt( er0*er0 + er1*er1 + 2.*cov01);
		    PresentationTex << frresult << " & " << frresult-SMfrResult <<" & " << efrresult << " & " << flresult << " & " << flresult-SMflResult << " & " << eflresult << " & " << f0result << " & " << f0result-SMf0Result << " & " << ef0result << " \\\\ " << endl;
		  } 
		  else {
		    frresult = 1.-minimizer.X()[0]-minimizer.X()[1];
		    double er0=minimizer.Errors()[0];
		    double er1=minimizer.Errors()[1];
		    double cov01 = minimizer.CovMatrix(0,1);
		    
		    efrresult = sqrt( er0*er0 + er1*er1 + 2.*cov01);
		    PresentationTex << frresult << " & " << frresult-SMfrResult <<" & " << efrresult << " & " << flresult << " & " << flresult-SMflResult << " & " << eflresult << " & " << f0result << " & " << f0result-SMf0Result << " & " << ef0result << " \\\\ " << endl;
		  }
		} 
		PresentationTex << " \\hline " << endl;
		
		LengthOfPresentationArray++;
		for (int ibinn=0; ibinn<CosThetaBinNumber; ibinn++){
		  delete genttbarhisto[ibinn];
		}
	      }//End of Minuit fitter loop
	      
	      TCHE++;
	      if(TCHE==13) {
		TCHP=1;
		SSVHE=1;
		SSVHP=1;
		CSV=1;
	      }	      

	    }//end of tche while loop
	    TCHE=13;	    	    
	    
	    std::string CosThetaDataString = "CosThetaData_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	    std::string CosThetaSignalString = "CosThetaSignal_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	    std::string CosThetaBckgString = "CosThetaBckg_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	    std::string CosThetaGeneratorString = "CosThetaGenerator_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	    
	    if(iDataSet == 0){
	      histo1D[CosThetaDataString]=new TH1F(CosThetaDataString.c_str(),CosThetaDataString.c_str(),CosThetaBinNumber,-1,1);
	      histo1D[CosThetaDataString]->SetDirectory(0);
	      histo1D[CosThetaSignalString]=new TH1F(CosThetaSignalString.c_str(),CosThetaSignalString.c_str(),CosThetaBinNumber,-1,1);
	      histo1D[CosThetaSignalString]->SetDirectory(0);
	      histo1D[CosThetaBckgString]=new TH1F(CosThetaBckgString.c_str(),CosThetaBckgString.c_str(),CosThetaBinNumber,-1,1);
	      histo1D[CosThetaBckgString]->SetDirectory(0);
	    }
	    
	    if(dataSetName.find("TTbarJets_SemiMu")==0 ){   //Filling of the genttbar histo
	      char hisname[100];
	      sprintf(hisname,"CosThetaGenerator_TCHE%s_TCHP%s_SSVHE%s_SSVHP%s_CSV%s", bTagValues[TCHE].c_str(),bTagValues[TCHP].c_str(),bTagValues[SSVHE].c_str(),bTagValues[SSVHP].c_str(),bTagValues[CSV].c_str());
	      for (int ibinn=0; ibinn<CosThetaBinNumber; ibinn++){
		genttbarhisto[ibinn]= new TNtuple(hisname,hisname,"costhgen:evtweight");
		genttbarhisto[ibinn]->SetDirectory(0);
	      }
	      for(int ii=0; ii<CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV].size();ii++){		
		for(int iBin=0; iBin< CosThetaBinNumber; iBin++){
		  
		  if(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] >= binEdge[iBin] && CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] < binEdge[iBin+1]){
		    double costhgood = CosThGen[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]; 
		    double thisWeight = EventCorrectionWeight[TCHE+TCHP+SSVHE+SSVHP+CSV][ii];
		    genttbarhisto[iBin]->Fill(costhgood , thisWeight) ; 
		  }
		  else if(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] ==1){ //1 is included in last bin
		    genttbarhisto[CosThetaBinNumber-1]->Fill(CosThGen[TCHE+TCHP+SSVHE+SSVHP+CSV][ii], EventCorrectionWeight[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]);
		  }	      	   
		}
	      }
	    }
	    
	    float scaleFactor = 1.;  //Make an array of the scaleFactor such that it can be different for every event !!
	    	    
	    for(int ii=0;ii<CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV].size();ii++){
	      //Change data to systematics since then nominal values will be reweighted!!
	      //if(dataSetName.find("WJets_Nominal") != 0)  //WJets syst
		if(dataSetName.find("Data") == 0 || dataSetName.find("JES") == 0)//Nominal and JES systematics
		histo1D[CosThetaDataString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],scaleFactor*(LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii])*Luminosity*dataSet->NormFactor());    
	      
	      if(dataSetName.find("TTbarJets_SemiMu") == 0 && dataSetName.find("JES_") != 0)
		histo1D[CosThetaSignalString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],Luminosity*scaleFactor*(LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii])*NominalNormFactor);
	      else if(dataSetName.find("Data") !=0 && dataSetName.find("JES") != 0 && dataSetName.find("Syst") != 0) 
		histo1D[CosThetaBckgString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],Luminosity*scaleFactor*(LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii])*NominalNormFactor); 
	    }
	    
	    if(iDataSet==(datasets.size()-1)){//Go in this loop when the last datasample is active
	      
	      MyChi2 myChi2 (histo1D[CosThetaDataString], histo1D[CosThetaSignalString], histo1D[CosThetaBckgString], 0.6671, 0.3325, 0.0004); 
	      
	      TMinuitMinimizer minimizer;
	      minimizer.SetFunction(myChi2);
	      minimizer.SetErrorDef(1.0);   // 1.0 for chi2, 0.5 for -logL
	      minimizer.SetVariable(0, "F0", 0.6671, 1.e-4);
	      minimizer.SetVariable(1, "FL", 0.3325, 1.e-4);
	      if (ndimen==3)  minimizer.SetVariable(2, "Normal", 1., 1.e-4); 
	      
	      bool isValid = minimizer.Minimize();
	      double f0result, frresult,flresult;
	      double ef0result, efrresult,eflresult;
	      if (isValid) {
		//minimizer.PrintResults();
		f0result =minimizer.X()[0]; ef0result= minimizer.Errors()[0];
		flresult= minimizer.X()[1]; eflresult= minimizer.Errors()[1];
		
		PresentationTex << PresentationOutput[TCHE+TCHP+SSVHE+SSVHP+CSV] << " & ";
		for(int ii=0; ii< nameDataSet.size(); ii++){
		  if(nameDataSet[ii].find("JES") != 0 && nameDataSet[ii].find("Syst") != 0 && ii < nameDataSet.size()-1){ //Only output for samples with no systematics
		    PresentationTex << NumberRemainingEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & ";
		  }
		  else if(ii == nameDataSet.size()-1 ){ 
		    PresentationTex << NumberRemainingEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & "; //For presentation this is not the end of the table, helicity values still need to be included !!
		    PresentationTex << NumberBLeptCorrectEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & ";
		  }
		}
		
		if (ndimen==3){
		  frresult = 1.-minimizer.X()[0]-minimizer.X()[1];
		  double er0=minimizer.Errors()[0];
		  double er1=minimizer.Errors()[1];
		  double cov01 = minimizer.CovMatrix(0,1);		  
		  efrresult = sqrt( er0*er0 + er1*er1 + 2.*cov01);

		  PresentationTex <<frresult<< " & " << frresult-SMfrResult <<" & " << efrresult << " & " << flresult << " & " << flresult-SMflResult << " & " << eflresult << " & " << f0result << " & " << f0result-SMf0Result << " & " << ef0result << " \\\\ " << endl;
		} 
		else {
		  frresult = 1.-minimizer.X()[0]-minimizer.X()[1];
		  double er0=minimizer.Errors()[0];
		  double er1=minimizer.Errors()[1];
		  double cov01 = minimizer.CovMatrix(0,1);		  
		  efrresult = sqrt( er0*er0 + er1*er1 + 2.*cov01);

		  PresentationTex <<frresult<< " & " << frresult-SMfrResult <<" & " << efrresult << " & " << flresult << " & " << flresult-SMflResult << " & " << eflresult << " & " << f0result << " & " << f0result-SMf0Result << " & " << ef0result << " \\\\ " << endl;
		}
	      } 
	      PresentationTex << " \\hline " << endl;
	      
	      for (int ibinn=0; ibinn<CosThetaBinNumber; ibinn++){
		delete genttbarhisto[ibinn];
	      }

	    }//End of Minuit fitter loop	      
	    
	    TCHP++;
	  }
	  TCHP=13;	  	  
	  
	  std::string CosThetaDataString = "CosThetaData_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	  std::string CosThetaSignalString = "CosThetaSignal_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	  std::string CosThetaBckgString = "CosThetaBckg_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	  std::string CosThetaGeneratorString = "CosThetaGenerator_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	  
	  if(iDataSet == 0){
	    histo1D[CosThetaDataString]=new TH1F(CosThetaDataString.c_str(),CosThetaDataString.c_str(),CosThetaBinNumber,-1,1);
	    histo1D[CosThetaDataString]->SetDirectory(0);
	    histo1D[CosThetaSignalString]=new TH1F(CosThetaSignalString.c_str(),CosThetaSignalString.c_str(),CosThetaBinNumber,-1,1);
	    histo1D[CosThetaSignalString]->SetDirectory(0);
	    histo1D[CosThetaBckgString]=new TH1F(CosThetaBckgString.c_str(),CosThetaBckgString.c_str(),CosThetaBinNumber,-1,1);
	    histo1D[CosThetaBckgString]->SetDirectory(0);
	  }
	  
	  if(dataSetName.find("TTbarJets_SemiMu")==0 ){   //Filling of the genttbar histo
	    char hisname[100];
	    sprintf(hisname,"CosThetaGenerator_TCHE%s_TCHP%s_SSVHE%s_SSVHP%s_CSV%s", bTagValues[TCHE].c_str(),bTagValues[TCHP].c_str(),bTagValues[SSVHE].c_str(),bTagValues[SSVHP].c_str(),bTagValues[CSV].c_str());
	    for (int ibinn=0; ibinn<CosThetaBinNumber; ibinn++){
	      genttbarhisto[ibinn]= new TNtuple(hisname,hisname,"costhgen:evtweight");
	      genttbarhisto[ibinn]->SetDirectory(0);
	    }
	    for(int ii=0; ii<CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV].size();ii++){		
	      for(int iBin=0; iBin< CosThetaBinNumber; iBin++){
		
		if(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] >= binEdge[iBin] && CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] < binEdge[iBin+1]){
		  double costhgood = CosThGen[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]; 
		  double thisWeight = EventCorrectionWeight[TCHE+TCHP+SSVHE+SSVHP+CSV][ii];
		  genttbarhisto[iBin]->Fill(costhgood , thisWeight) ; 
		}
		else if(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] ==1){ //1 is included in last bin
		  genttbarhisto[CosThetaBinNumber-1]->Fill(CosThGen[TCHE+TCHP+SSVHE+SSVHP+CSV][ii], EventCorrectionWeight[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]);
		}	      	   
	      }
	    }
	  }
	  
	  float scaleFactor = 1.;  //Make an array of the scaleFactor such that it can be different for every event !!
	  	  
	  for(int ii=0;ii<CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV].size();ii++){
	    //Change data to systematics since then nominal values will be reweighted!!
	    //if(dataSetName.find("WJets_Nominal") != 0)  //WJets syst
	      if(dataSetName.find("Data") == 0 || dataSetName.find("JES") == 0)//Nominal and JES systematics	    
	      histo1D[CosThetaDataString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],scaleFactor*LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]*Luminosity*dataSet->NormFactor());    
	    
	    if(dataSetName.find("TTbarJets_SemiMu") == 0)
	      histo1D[CosThetaSignalString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],Luminosity*scaleFactor*LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]*NominalNormFactor);
	    else if(dataSetName.find("WJets_Nominal")==0 || dataSetName.find("TTbarJets_Other") ==0) 
	      histo1D[CosThetaBckgString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],Luminosity*scaleFactor*LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]*NominalNormFactor); 
	  }
	  
	  if(iDataSet==(datasets.size()-1)){//Go in this loop when the last datasample is active

	    
	    MyChi2 myChi2 (histo1D[CosThetaDataString], histo1D[CosThetaSignalString], histo1D[CosThetaBckgString], 0.6671, 0.3325, 0.0004); 
	    
	    TMinuitMinimizer minimizer;
	    minimizer.SetFunction(myChi2);
	    minimizer.SetErrorDef(1.0);   // 1.0 for chi2, 0.5 for -logL
	    minimizer.SetVariable(0, "F0", 0.6671, 1.e-4);
	    minimizer.SetVariable(1, "FL", 0.3325, 1.e-4);
	    if (ndimen==3)  minimizer.SetVariable(2, "Normal", 1., 1.e-4); 
	    
	    bool isValid = minimizer.Minimize();
	    double f0result, frresult,flresult;
	    double ef0result, efrresult,eflresult;
	    if (isValid) {
	      //minimizer.PrintResults();
	      f0result =minimizer.X()[0]; ef0result= minimizer.Errors()[0];
	      flresult= minimizer.X()[1]; eflresult= minimizer.Errors()[1];
	      
	      
	      PresentationTex << PresentationOutput[TCHE+TCHP+SSVHE+SSVHP+CSV] << " & ";
	      for(int ii=0; ii< nameDataSet.size(); ii++){
		if(nameDataSet[ii].find("JES") != 0 && nameDataSet[ii].find("Syst") != 0 && ii < nameDataSet.size()-1){ //Only output for samples with no systematics
		  PresentationTex << NumberRemainingEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & ";
		}
		else if(ii == nameDataSet.size()-1 ){ 
		  PresentationTex << NumberRemainingEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & "; //For presentation this is not the end of the table, helicity values still need to be included !!
		  PresentationTex << NumberBLeptCorrectEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & ";
		}
	      }
	      
	      if (ndimen==3){
		frresult = 1.-minimizer.X()[0]-minimizer.X()[1];
		double er0=minimizer.Errors()[0];
		double er1=minimizer.Errors()[1];
		double cov01 = minimizer.CovMatrix(0,1);		  
		efrresult = sqrt( er0*er0 + er1*er1 + 2.*cov01);
		
		PresentationTex << frresult << " & " << frresult-SMfrResult <<" & " << efrresult << " & " << flresult << " & " << flresult-SMflResult << " & " << eflresult << " & " << f0result << " & " << f0result-SMf0Result << " & " << ef0result << " \\\\ " << endl;
	      } 
	      else {
		frresult = 1.-minimizer.X()[0]-minimizer.X()[1];
		double er0=minimizer.Errors()[0];
		double er1=minimizer.Errors()[1];
		double cov01 = minimizer.CovMatrix(0,1);		
		efrresult = sqrt( er0*er0 + er1*er1 + 2.*cov01);

		PresentationTex << frresult << " & " << frresult-SMfrResult <<" & " << efrresult << " & " << flresult << " & " << flresult-SMflResult << " & " << eflresult << " & " << f0result << " & " << f0result-SMf0Result << " & " << ef0result << " \\\\ " << endl;
	      }
	    } 
	    PresentationTex << " \\hline " << endl;
	    
	    for (int ibinn=0; ibinn<CosThetaBinNumber; ibinn++){
	      delete genttbarhisto[ibinn];
	    }

	  }//End of Minuit fitter loop	      
	  
	  SSVHE++;
	}
	SSVHE=13;		
	
	std::string CosThetaDataString = "CosThetaData_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	std::string CosThetaSignalString = "CosThetaSignal_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	std::string CosThetaBckgString = "CosThetaBckg_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	std::string CosThetaGeneratorString = "CosThetaGenerator_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	
	if(iDataSet == 0){
	  histo1D[CosThetaDataString]=new TH1F(CosThetaDataString.c_str(),CosThetaDataString.c_str(),CosThetaBinNumber,-1,1);
	  histo1D[CosThetaDataString]->SetDirectory(0);
	  histo1D[CosThetaSignalString]=new TH1F(CosThetaSignalString.c_str(),CosThetaSignalString.c_str(),CosThetaBinNumber,-1,1);
	  histo1D[CosThetaSignalString]->SetDirectory(0);
	  histo1D[CosThetaBckgString]=new TH1F(CosThetaBckgString.c_str(),CosThetaBckgString.c_str(),CosThetaBinNumber,-1,1);
	  histo1D[CosThetaBckgString]->SetDirectory(0);
	}
	
	if(dataSetName.find("TTbarJets_SemiMu")==0 ){   //Filling of the genttbar histo
	  char hisname[100];
	  sprintf(hisname,"CosThetaGenerator_TCHE%s_TCHP%s_SSVHE%s_SSVHP%s_CSV%s", bTagValues[TCHE].c_str(),bTagValues[TCHP].c_str(),bTagValues[SSVHE].c_str(),bTagValues[SSVHP].c_str(),bTagValues[CSV].c_str());
	  for (int ibinn=0; ibinn<CosThetaBinNumber; ibinn++){
	    genttbarhisto[ibinn]= new TNtuple(hisname,hisname,"costhgen:evtweight");
	    genttbarhisto[ibinn]->SetDirectory(0);
	  }
	  for(int ii=0; ii<CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV].size();ii++){		
	    for(int iBin=0; iBin< CosThetaBinNumber; iBin++){
	      
	      if(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] >= binEdge[iBin] && CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] < binEdge[iBin+1]){
		double costhgood = CosThGen[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]; 
		double thisWeight = EventCorrectionWeight[TCHE+TCHP+SSVHE+SSVHP+CSV][ii];
		genttbarhisto[iBin]->Fill(costhgood , thisWeight) ; 
	      }
	      else if(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] ==1){ //1 is included in last bin
		genttbarhisto[CosThetaBinNumber-1]->Fill(CosThGen[TCHE+TCHP+SSVHE+SSVHP+CSV][ii], EventCorrectionWeight[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]);
	      }	      	   
	    }
	  }
	}
	
	float scaleFactor = 1.;  //Make an array of the scaleFactor such that it can be different for every event !!
	
	for(int ii=0;ii<CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV].size();ii++){
	  //Change data to systematics since then nominal values will be reweighted!!
	  //if(dataSetName.find("WJets_Nominal") != 0)  //WJets syst
	   if(dataSetName.find("Data") == 0 || dataSetName.find("JES") == 0)//Nominal and JES systematics
	    histo1D[CosThetaDataString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],scaleFactor*LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]*Luminosity*dataSet->NormFactor());    
	  
	  if(dataSetName.find("TTbarJets_SemiMu") == 0)
	    histo1D[CosThetaSignalString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],Luminosity*scaleFactor*LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]*NominalNormFactor);
	  else if(dataSetName.find("WJets_Nominal")==0 || dataSetName.find("TTbarJets_Other") ==0) //Syst
	    histo1D[CosThetaBckgString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],Luminosity*scaleFactor*LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]*NominalNormFactor); 
	}
	
	if(iDataSet==(datasets.size()-1)){//Go in this loop when the last datasample is active
	  
	  MyChi2 myChi2 (histo1D[CosThetaDataString], histo1D[CosThetaSignalString], histo1D[CosThetaBckgString], 0.6671, 0.3325, 0.0004); 
	  
	  TMinuitMinimizer minimizer;
	  minimizer.SetFunction(myChi2);
	  minimizer.SetErrorDef(1.0);   // 1.0 for chi2, 0.5 for -logL
	  minimizer.SetVariable(0, "F0", 0.6671, 1.e-4);
	  minimizer.SetVariable(1, "FL", 0.3325, 1.e-4);
	  if (ndimen==3)  minimizer.SetVariable(2, "Normal", 1., 1.e-4); 
	  
	  bool isValid = minimizer.Minimize();
	  double f0result, frresult,flresult;
	  double ef0result, efrresult,eflresult;
	  if (isValid) {
	    //minimizer.PrintResults();
	    f0result =minimizer.X()[0]; ef0result= minimizer.Errors()[0];
	    flresult= minimizer.X()[1]; eflresult= minimizer.Errors()[1];
	    
	    PresentationTex << PresentationOutput[TCHE+TCHP+SSVHE+SSVHP+CSV] << " & ";
	    for(int ii=0; ii< nameDataSet.size(); ii++){
	      if(nameDataSet[ii].find("JES") != 0 && nameDataSet[ii].find("Syst") != 0 && ii < nameDataSet.size()-1){ //Only output for samples with no systematics
		PresentationTex << NumberRemainingEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & ";
	      }
	      else if(ii == nameDataSet.size()-1 ){ 
		PresentationTex << NumberRemainingEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & "; //For presentation this is not the end of the table, helicity values still need to be included !!
		PresentationTex << NumberBLeptCorrectEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & ";
	      }
	    }
	    
	    if (ndimen==3){
	      frresult = 1.-minimizer.X()[0]-minimizer.X()[1];
	      double er0=minimizer.Errors()[0];
	      double er1=minimizer.Errors()[1];
	      double cov01 = minimizer.CovMatrix(0,1);		  
	      efrresult = sqrt( er0*er0 + er1*er1 + 2.*cov01);
	      
	      PresentationTex << frresult << " & " << frresult-SMfrResult <<" & " << efrresult << " & " << flresult << " & " << flresult-SMflResult << " & " << eflresult << " & " << f0result << " & " << f0result-SMf0Result << " & " << ef0result << " \\\\ " << endl;
	    } 
	    else {
	      frresult = 1.-minimizer.X()[0]-minimizer.X()[1];
	      double er0=minimizer.Errors()[0];
	      double er1=minimizer.Errors()[1];
	      double cov01 = minimizer.CovMatrix(0,1);	      
	      efrresult = sqrt( er0*er0 + er1*er1 + 2.*cov01);
	      
	      PresentationTex << frresult << " & " << frresult-SMfrResult <<" & " << efrresult << " & " << flresult << " & " << flresult-SMflResult << " & " << eflresult << " & " << f0result << " & " << f0result-SMf0Result << " & " << ef0result << " \\\\ " << endl;
	    }
	  } 
	  PresentationTex << " \\hline " << endl;
	  
	  for (int ibinn=0; ibinn<CosThetaBinNumber; ibinn++){
	    delete genttbarhisto[ibinn];
	  }

	}//End of Minuit fitter loop	
	
	SSVHP++;
      }    
      SSVHP=13;            
      
      std::string CosThetaDataString = "CosThetaData_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
      std::string CosThetaSignalString = "CosThetaSignal_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
      std::string CosThetaBckgString = "CosThetaBckg_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
      std::string CosThetaGeneratorString = "CosThetaGenerator_TCHE"+bTagValues[TCHE]+"_TCHP"+bTagValues[TCHP]+"_SSVHE"+bTagValues[SSVHE]+"_SSVHP"+bTagValues[SSVHP]+"_CSV"+bTagValues[CSV];
	
      if(iDataSet == 0){
	histo1D[CosThetaDataString]=new TH1F(CosThetaDataString.c_str(),CosThetaDataString.c_str(),CosThetaBinNumber,-1,1);
	histo1D[CosThetaDataString]->SetDirectory(0);
	histo1D[CosThetaSignalString]=new TH1F(CosThetaSignalString.c_str(),CosThetaSignalString.c_str(),CosThetaBinNumber,-1,1);
	histo1D[CosThetaSignalString]->SetDirectory(0);
	histo1D[CosThetaBckgString]=new TH1F(CosThetaBckgString.c_str(),CosThetaBckgString.c_str(),CosThetaBinNumber,-1,1);
	histo1D[CosThetaBckgString]->SetDirectory(0);
      }
	
      if(dataSetName.find("TTbarJets_SemiMu")==0 ){   //Filling of the genttbar histo
	char hisname[100];
	sprintf(hisname,"CosThetaGenerator_TCHE%s_TCHP%s_SSVHE%s_SSVHP%s_CSV%s", bTagValues[TCHE].c_str(),bTagValues[TCHP].c_str(),bTagValues[SSVHE].c_str(),bTagValues[SSVHP].c_str(),bTagValues[CSV].c_str());
	for (int ibinn=0; ibinn<CosThetaBinNumber; ibinn++){
	  genttbarhisto[ibinn]= new TNtuple(hisname,hisname,"costhgen:evtweight");
	  genttbarhisto[ibinn]->SetDirectory(0);
	}
	for(int ii=0; ii<CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV].size();ii++){		
	  for(int iBin=0; iBin< CosThetaBinNumber; iBin++){
	      
	    if(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] >= binEdge[iBin] && CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] < binEdge[iBin+1]){
	      double costhgood = CosThGen[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]; 
	      double thisWeight = EventCorrectionWeight[TCHE+TCHP+SSVHE+SSVHP+CSV][ii];
	      genttbarhisto[iBin]->Fill(costhgood , thisWeight) ; 
	    }
	    else if(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] ==1){ //1 is included in last bin
	      genttbarhisto[CosThetaBinNumber-1]->Fill(CosThGen[TCHE+TCHP+SSVHE+SSVHP+CSV][ii], EventCorrectionWeight[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]);
	    }	      	   
	  }
	}
      }
	
      float scaleFactor = 1.;  //Make an array of the scaleFactor such that it can be different for every event !!
	
      for(int ii=0;ii<CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV].size();ii++){
	//Change data to systematics since then nominal values will be reweighted!!
	//if(dataSetName.find("WJets_Nominal") != 0)  //WJets syst
	  if(dataSetName.find("Data") == 0 || dataSetName.find("JES") == 0)//Nominal and JES systematics
	  histo1D[CosThetaDataString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],scaleFactor*LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]*Luminosity*dataSet->NormFactor());    
	  
	if(dataSetName.find("TTbarJets_SemiMu") == 0)
	  histo1D[CosThetaSignalString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],Luminosity*scaleFactor*LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]*NominalNormFactor);
	else if(dataSetName.find("WJets_Nominal")==0 || dataSetName.find("TTbarJets_Other") ==0) //Syst
	  histo1D[CosThetaBckgString]->Fill(CosThetaValues[TCHE+TCHP+SSVHE+SSVHP+CSV][ii],Luminosity*scaleFactor*LumiWeightVector[TCHE+TCHP+SSVHE+SSVHP+CSV][ii]*NominalNormFactor); 
      }
	
      if(iDataSet==(datasets.size()-1)){//Go in this loop when the last datasample is active

	  
	MyChi2 myChi2 (histo1D[CosThetaDataString], histo1D[CosThetaSignalString], histo1D[CosThetaBckgString], 0.6671, 0.3325, 0.0004); 
	  
	TMinuitMinimizer minimizer;
	minimizer.SetFunction(myChi2);
	minimizer.SetErrorDef(1.0);   // 1.0 for chi2, 0.5 for -logL
	minimizer.SetVariable(0, "F0", 0.6671, 1.e-4);
	minimizer.SetVariable(1, "FL", 0.3325, 1.e-4);
	if (ndimen==3)  minimizer.SetVariable(2, "Normal", 1., 1.e-4); 
	  
	bool isValid = minimizer.Minimize();
	double f0result, frresult,flresult;
	double ef0result, efrresult,eflresult;
	if (isValid) {
	  //minimizer.PrintResults();
	  f0result =minimizer.X()[0]; ef0result= minimizer.Errors()[0];
	  flresult= minimizer.X()[1]; eflresult= minimizer.Errors()[1];
	    
	  PresentationTex << PresentationOutput[TCHE+TCHP+SSVHE+SSVHP+CSV] << " & ";
	  for(int ii=0; ii< nameDataSet.size(); ii++){
	    if(nameDataSet[ii].find("JES") != 0 && nameDataSet[ii].find("Syst") != 0 && ii < nameDataSet.size()-1){ //Only output for samples with no systematics
	      PresentationTex << NumberRemainingEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & ";
	    }
	    else if(ii == nameDataSet.size()-1 ){ 
	      PresentationTex << NumberRemainingEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & "; //For presentation this is not the end of the table, helicity values still need to be included !!
	      PresentationTex << NumberBLeptCorrectEvents[TCHE+TCHP+SSVHE+SSVHP+CSV][ii] << " & ";
	    }
	  }
	    
	  if (ndimen==3){
	    frresult = 1.-minimizer.X()[0]-minimizer.X()[1];
	    double er0=minimizer.Errors()[0];
	    double er1=minimizer.Errors()[1];
	    double cov01 = minimizer.CovMatrix(0,1);		  
	    efrresult = sqrt( er0*er0 + er1*er1 + 2.*cov01);
	      
	    PresentationTex << frresult << " & " << frresult-SMfrResult <<" & " << efrresult << " & " << flresult << " & " << flresult-SMflResult << " & " << eflresult << " & " << f0result << " & " << f0result-SMf0Result << " & " << ef0result << " \\\\ " << endl;
	  } 
	  else {
	    frresult = 1.-minimizer.X()[0]-minimizer.X()[1];
	    double er0=minimizer.Errors()[0];
	    double er1=minimizer.Errors()[1];
	    double cov01 = minimizer.CovMatrix(0,1);	      
	    efrresult = sqrt( er0*er0 + er1*er1 + 2.*cov01);
	      
	    PresentationTex << frresult << " & " << frresult-SMfrResult <<" & " << efrresult << " & " << flresult << " & " << flresult-SMflResult << " & " << eflresult << " & " << f0result << " & " << f0result-SMf0Result << " & " << ef0result << " \\\\ " << endl;
	  }
	} 
	PresentationTex << " \\hline " << endl;
	  
	for (int ibinn=0; ibinn<CosThetaBinNumber; ibinn++){
	  delete genttbarhisto[ibinn];
	}

      }//End of Minuit fitter loop	
      
      CSV++;
    }
        
    outFile << endl << "Finished event-by-event info, end of file!" << endl;
    outFile.close();
    
    inFile->Close();
    delete inFile;
    
  } // end loop over datasets

  //Selection tables:
  selecTableMacro.TableCalculator(false,true,true,true);
  string selectiontableMacro = "SelectionTable_Analysis_Macro";
  if(argc >= 3){
    string sample = string(argv[2]);
    selectiontableMacro = selectiontableMacro+"_"+sample;
  }
  selectiontableMacro = selectiontableMacro+".tex";
  selecTableMacro.Write(selectiontableMacro.c_str(),1);

  //Write output of number of events:
  cout << " " << endl;
  cout << " TTbar SemiMu " << endl;
  std::cout << " Comparing efficiency of selected events " << endl;
  cout << " NumberEventsBeforeCuts : " << NumberEventsBeforeCuts << endl;
  cout << " NumberEventsAfterbTag : " <<  NumberEventsAfterbTag << endl;
  cout << " NumberEventsAfterTransverseMass : " << NumberEventsAfterTransverseMass  <<endl;
  cout << " NumberSelectedEvents : " <<  NumberSelectedEvents << endl;
  cout << " NumberUsedEvents : " <<  NumberUsedEvents << endl;
  cout << " NumberUsedCorrectEvents : " << NumberUsedCorrectEvents << endl;
  cout << " NumberUsedWrongEvents : " <<  NumberUsedWrongEvents  << endl;
  cout << "                -------        " << endl;
  cout << " NumberDataEventsBeforeCuts : " << NumberDataEventsBeforeCuts << endl;
  cout << " NumberSelectedDataEvents : " << NumberSelectedDataEvents  << endl;
  cout << " NumberUsedDataEvents : " << NumberUsedDataEvents  << endl;
  std::cout << " " << endl;

//   //Calculate ratio for cut on probability of Kinematic Fit:
//   histo1D["CosThetaNobTagProbCut1D"]->Scale(100./(histo1D["CosThetaNobTagProbCut1D"]->Integral()));
//   histo1D["CosThetaNobTag1D"]->Scale(100./(histo1D["CosThetaNobTag1D"]->Integral()));
  
//   histo1D["ratioHisto"] = (TH1F*) histo1D["CosThetaNobTagProbCut1D"]->Clone();
//   //ratioHisto->SetName("Ratio");
//   //ratioHisto->Reset();
//   histo1D["CosThetaNobTagClone"] = (TH1F*) histo1D["CosThetaNobTag1D"]->Clone();
//   histo1D["ratioHisto"]->Divide(histo1D["CosThetaNobTagClone"]);
//   histo1D["ratioHisto"]->SaveAs("ratioHisto.root");
//   histo1D["ratioHisto"]->SaveAs("ratioHisto.png");

//   histo1D["KinFitRatio"] = (TH1F*) histo1D["KinFitProbCorrectBLept"]->Clone();
//   histo1D["KinFitProbWrongBLeptClone"] = (TH1F*) histo1D["KinFitProbWrongBLept"]->Clone();
//   histo1D["KinFitRatio"]->Divide(histo1D["KinFitProbWrongBLeptClone"]);
//   histo1D["KinFitRatio"]->SaveAs("KinFitRatioHisto.root");
//   histo1D["KinFitRatio"]->SaveAs("KinFitRatioHisto.png");

  PresentationTex << " \\end{tabular} " << endl;
  PresentationTex << " \\end{center} " << endl;
  PresentationTex << " \\renewcommand{\\arraystretch}{0.9} " << endl;
  PresentationTex << " \\end{tiny} " << endl;
  PresentationTex << " \\end{table} " << endl;
  PresentationTex.close();		

  cout << "Finished running over all datasets..." << endl;
  fout->cd();

  ///////////////////
  // Writting
  //////////////////  
    
  cout << "Writing out..." << endl;
  fout->cd();

  TDirectory* tprofdir = fout->mkdir("TProfile_histograms");
  TDirectory* th1dir = fout->mkdir("1D_histograms");
  TDirectory* th2dir = fout->mkdir("2D_histograms_graphs");

  fout->cd();

  th1dir->cd();
  // Write 1D histo's
  for(std::map<std::string,TH1F*>::const_iterator it = histo1D.begin(); it != histo1D.end(); it++){
    string name = it->first;
    if(name.find("CosTheta") != 0){
      TH1F *temp = it->second;
      temp->Write();
      TCanvas* tempCanvas = TCanvasCreator(temp, it->first);
      tempCanvas->SaveAs( (pathPNG+it->first+".png").c_str() );
      tempCanvas->Write();
      //		tempCanvas->SaveAs( (pathPNG+it->first+".pdf").c_str() );
    }
    else{
      TH1F *temp = it->second;
      temp->Write();
      TCanvas* tempCanvas = TCanvasCreator(temp, it->first);
      tempCanvas->SaveAs( (pathPNG+"/CosThetaPlots/"+it->first+".png").c_str() );
      //tempCanvas->Write();
    }
  }  

  fout->cd();
  
  for(map<string,MultiSamplePlot*>::const_iterator it = MSPlot.begin(); it != MSPlot.end(); it++){
    MultiSamplePlot *temp = it->second;
    string name = it->first;
    temp->Draw(false, name, false, true, true, true, true,1,true);
    temp->Write(fout, name, true, pathPNG+"MSPlot/");
  }

  // 2D
  th2dir->cd();
  for(std::map<std::string,TH2F*>::const_iterator it = histo2D.begin(); it != histo2D.end(); it++){
    TH2F *temp = it->second;
    temp->Write();
    TCanvas* tempCanvas = TCanvasCreator(temp, it->first);
    tempCanvas->SaveAs( (pathPNG+it->first+".png").c_str() );
    //		tempCanvas->SaveAs( (pathPNG+it->first+".pdf").c_str() );
  }

  //Write TGraphAsymmErrors
  fout->cd();
  for(map<string,TGraphAsymmErrors*>::const_iterator it = graphAsymmErr.begin(); it != graphAsymmErr.end(); it++){
    TGraphAsymmErrors *temp = it->second;
    temp->Write();
    TCanvas* tempCanvas = TCanvasCreator(temp, it->first);
    tempCanvas->SaveAs( (pathPNG+it->first+".png").c_str() );
    //		tempCanvas->SaveAs( (pathPNG+it->first+".pdf").c_str() );
  }
  
  //Write TGraphErrors
  fout->cd();
  for(map<string,TGraphErrors*>::const_iterator it = graphErr.begin(); it != graphErr.end(); it++){
    TGraphErrors *temp = it->second;
    temp->Write();
    TCanvas* tempCanvas = TCanvasCreator(temp, it->first);
    tempCanvas->SaveAs( (pathPNG+it->first+".png").c_str() );
    //    tempCanvas->SaveAs( (pathPNG+it->first+".pdf").c_str() );
  }

  fout->Close();
  delete fout;
  
  cout << "It took us " << ((double)clock() - start) / CLOCKS_PER_SEC << " to run the program" << endl;
  
  cout << "********************************************" << endl;
  cout << "           End of the program !!            " << endl;
  cout << "           hasn't crashed yet ;-)           " << endl;
  cout << "********************************************" << endl;
  
  return 0;
}
