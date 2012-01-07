#ifndef JetCombiner_h
#define JetCombiner_h

// Class that takes care of the Jet-parton matching and of the MVA-based jet combination

#include <iostream>
#include <iomanip>
#include <vector>
#include <fstream>
#include <cstdio>

#include "TLorentzVector.h"
#include "TH1F.h"

#include <sys/stat.h>

#include "TopTreeAnalysis/Tools/interface/MVAComputer.h"
#include "TopTreeAnalysis/Tools/interface/MVATrainer.h"
#include "TopTreeAnalysis/Tools/interface/PlottingTools.h"
#include "TopTreeAnalysis/MCInformation/interface/ResolutionFit.h"
#include "TopTreeAnalysis/Tools/interface/MultiSamplePlot.h"
#include "TopTreeAnalysis/MCInformation/interface/JetPartonMatching.h"
#include "TopTreeAnalysis/JESMeasurement/interface/ExpCorrCalculator.h"

#include "TopTreeProducer/interface/TRootJet.h"
#include "TopTreeProducer/interface/TRootMuon.h"
#include "TopTreeProducer/interface/TRootElectron.h"
#include "TopTreeProducer/interface/TRootEvent.h"
#include "TopTreeProducer/interface/TRootGenEvent.h"

using namespace std;

struct MVAValues {
  string MVAAlgo;
  float MVAValue;
  int WJet1;
  int WJet2;
  int HadrBJet;
  int LeptBJet;
} ;

class JetCombiner {

  public:
    JetCombiner(bool trainMVA, float Luminosity, const vector<Dataset*>& datasets, string MVAMethod = "Likelihood", bool Tprime = false, string MVAfilePostfix = "", string postfix = "");
    ~JetCombiner();
    void ProcessEvent(Dataset* dataSet, const vector<TRootMCParticle*> mcParticles, const vector<TRootJet*> selectedJets, const TLorentzVector* selectedMuon, vector<TRootElectron*> vectEl, vector<TRootMuon*> vectMu, const TRootGenEvent* genEvt, float scaleFactor=1, bool TprimeEvaluation=false);
    void FillResolutions(ResolutionFit* resFitLightJets, ResolutionFit* resFitBJets, ResolutionFit* resFitBJets_B=0, ResolutionFit* resFitBJets_Bbar=0);
    void FillExpCorr(ExpCorrCalculator* expCorr, const vector<TRootMuon*> muons, const vector<TRootElectron*> electrons, float maxMVA);
    pair<float, vector<unsigned int> > getMVAValue(string MVAMethod, int rank); // rank 1 means the highest MVA value for this method, rank 2 the second highest, ...
    vector<unsigned int> GetGoodJetCombination(); // Good according to MC!
    bool isGoodJetCombination(string MVAMethod, int rank, bool fullcombination); // check if a certain MVA jet combination is	correct (correct for the 3 hadronic top jets OR correct for the full jet combination)
    bool All4JetsMatched_MCdef() { return all4JetsMatched_MCdef_; }; // true if the 4 highest pT jets are matched to the TTbar partons according to the MC
    bool HadronicTopJetsMatched_MCdef() { return hadronictopJetsMatched_MCdef_; }; // true if the 3 hadronic top jets are matched to		the	partons of the hadronic top according to the MC
    float GetRelEDiffJetPartonB() { return relDiffEJetParton_b_; };
    float GetRelEDiffJetPartonL1() { return relDiffEJetParton_l1_; };
    float GetRelEDiffJetPartonL2() { return relDiffEJetParton_l2_; };
    TLorentzVector GetHadrBQuark();
    TLorentzVector GetLightQuark1();
    TLorentzVector GetLightQuark2();
    TLorentzVector GetLeptBQuark();
    void EndJob(); // Do all the stuff which needs to be done after running over all the events has finished (except the plotting stuff)
    void Write(TFile* fout, bool savePNG = false, string pathPNG = string(""));
    
  private:
    map<string, MultiSamplePlot*> MSPlot_;
    map<string, TH1F*> histo1D_;
    map<string, TH2F*> histo2D_;
    map<string,TGraphAsymmErrors*> graphAsymmErr_;
    
    vector<Dataset*> datasets_;
    float Luminosity_;
    
    bool all4JetsMatched_MCdef_, hadronictopJetsMatched_MCdef_;
    bool all4JetsMatched_MVAdef_, hadronictopJetsMatched_MVAdef_;

    bool Tprime_; // true if tprime analysis, false if JES/mtop analysis
    string MVAfilePostfix_;
    string postfix_;
		
    ExpCorrCalculator* expCorrIncl_;
    
    float relDiffEJetParton_b_;
    float relDiffEJetParton_l1_;
    float relDiffEJetParton_l2_;
   
    bool trainMVA_; // true if the MVA needs to be trained, false if the trained MVA will be used to compute stuff
    MVATrainer* trainer_;
    MVAComputer* computer_;

    vector<MVAValues> vectorMVA_;
    
    pair<unsigned int, unsigned int> leptonicBJet_, hadronicBJet_, hadronicWJet1_, hadronicWJet2_; //First index is the JET number, second one is the parton
    vector<TRootMCParticle*> mcParticlesMatching_; // MCParticles used for the matching, need to be stored to be used after the ProcessEvent fuction was called
    vector<TRootJet*> selectedJets_; // need to be stored to be used after the ProcessEvent function was called
    
    bool EndJobWasRun_;
    
};

#endif
