#include <TROOT.h>
#include <TFile.h>
#include <TTree.h>
#include <TSystem.h>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/EDAnalyzer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "CommonTools/UtilAlgos/interface/TFileService.h"
#include "PhysicsTools/FWLite/interface/TFileService.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "SimDataFormats/CrossingFrame/interface/CrossingFrame.h"
#include "DataFormats/HepMCCandidate/interface/GenParticle.h"
#include "DataFormats/JetReco/interface/GenJet.h"
#include "DataFormats/JetReco/interface/PFJet.h"
#include "DataFormats/Common/interface/SortedCollection.h"

#include "FastTiming/RecoTreeUtils/interface/JetWithFT.h"
#include "FastTiming/RecoTreeUtils/interface/FTFile.h"

typedef std::vector<reco::TrackBaseRef >::const_iterator trackRef_iterator;

using namespace std;

//****************************************************************************************

class RecoFastTiming : public edm::EDAnalyzer
{
public:
    explicit RecoFastTiming(const edm::ParameterSet&);
    ~RecoFastTiming() {};

    //---utils---
    int          FindGenVtxs();
    void         MatchGenVtx();
    int          FindVtxSeedParticle(PFCandidateWithFT* particle);
    void         AssignChargedToVtxs(vector<PFCandidateWithFT*>* charged_cand);
    void         AssignNeutralToVtxs(vector<PFCandidateWithFT*>* neutral_cand);
    void         ProcessVertices();
    void         ProcessParticles(vector<PFCandidateWithFT*>* particles, int iVtx);
    void         ProcessJets(vector<EcalRecHit>* recVectEK);
    void         ComputeSumEt();
        
private:
    virtual void beginJob();
    virtual void analyze(const edm::Event&, const edm::EventSetup&);
    virtual void endJob();

    int iEvent_;
    const CaloGeometry* skGeometry_;
    const CaloGeometry* ebGeometry_;
    const MagneticField* magField_;
    //---output file---
    edm::Service<TFileService> fs_;
    FTFile* outFile_;
    //---input tag---
    edm::InputTag genVtxInputTag_;
    edm::InputTag genJetsInputTag_;
    edm::InputTag recoJetsInputTag_;
    //---objects interfaces---
    edm::ESHandle<MagneticField> magFieldHandle_;             
    edm::ESHandle<CaloGeometry> geoHandleEK_;
    edm::ESHandle<CaloGeometry> geoHandleEB_;
    edm::Handle<vector<SimVertex> > genSigVtxHandle_;
    //edm::Handle<PCrossingFrame<SimVertex> > genVtxsHandle_;    
    edm::Handle<vector<SimVertex> > genVtxsHandle_;
    edm::Handle<vector<reco::Vertex> > recoVtxHandle_;
    edm::Handle<vector<reco::PFCandidate> > candHandle_;
    edm::Handle<vector<reco::GenParticle> > genPartHandle_;
    edm::Handle<vector<reco::GenJet> > genJetsHandle_;
    edm::Handle<vector<reco::PFJet> > recoJetsHandle_;
    edm::Handle<edm::SortedCollection<EcalRecHit, 
                                      edm::StrictWeakOrdering<EcalRecHit > > > recSortEK_;
    edm::Handle<edm::SortedCollection<EcalRecHit, 
                                      edm::StrictWeakOrdering<EcalRecHit > > > recSortEB_;
    //---FT objects---
    map<int, const SimVertex*> genVtxsMap_;
    vector<VertexWithFT> recoVtxCollection_;
    vector<PFCandidateWithFT> particlesCollection_;
    MVATimeComputer* mvaComputer_;
    //---Options---
    float tRes_;
    float dzCut_;       
    float ptCut_;
    float pz2Cut_;       
    bool saveParticles_;
    bool saveAllRecHits_;
    bool saveVertices_;
};

RecoFastTiming::RecoFastTiming(const edm::ParameterSet& Config)
{
    genVtxInputTag_ = Config.getUntrackedParameter<edm::InputTag>("genVtxTag");
    genJetsInputTag_ = Config.getUntrackedParameter<edm::InputTag>("genJetsTag");
    recoJetsInputTag_ = Config.getUntrackedParameter<edm::InputTag>("recoJetsTag");
    tRes_ = Config.getUntrackedParameter<double>("timeResSmearing");
    dzCut_ = Config.getUntrackedParameter<double>("dzCut");
    ptCut_ = Config.getUntrackedParameter<double>("ptCut");
    pz2Cut_ = Config.getUntrackedParameter<double>("pz2Cut");
    saveParticles_ = Config.getUntrackedParameter<bool>("saveParticles");
    saveAllRecHits_ = Config.getUntrackedParameter<bool>("saveAllRecHits");
    saveVertices_ = Config.getUntrackedParameter<bool>("saveVertices");

    mvaComputer_ = new MVATimeComputer("/afs/cern.ch/user/s/spigazzi/work/FastTiming_TP/CMSSW_6_2_0_SLHC25_patch1/src/FastTiming/RecoTreeUtils/weights/TMVARegression_BDTG.weights.xml");
}

void RecoFastTiming::beginJob()
{
    iEvent_=1;
    outFile_ = new FTFile(&fs_->file());
}

void RecoFastTiming::endJob()
{    
    outFile_->cd();
    outFile_->particlesTree.Write("ft_particles");
    outFile_->verticesTree.Write("ft_vertices");
    outFile_->globalTree.Write("ft_global");
    outFile_->jetsTree.Write("ft_jets");
}

void RecoFastTiming::analyze(const edm::Event& Event, const edm::EventSetup& Setup)
{
    if(!outFile_)
    {
        cout << "WARNING: output file is NULL" << endl;
        return;
    }
    outFile_->particlesTree.event_n = iEvent_;
    ++iEvent_;
    //---get the magnetic field---
    Setup.get<IdealMagneticFieldRecord>().get(magFieldHandle_);
    magField_ = magFieldHandle_.product();
    //---get the geometry(s)---
    Setup.get<CaloGeometryRecord>().get(geoHandleEK_);
    skGeometry_ = geoHandleEK_.product();
    Setup.get<CaloGeometryRecord>().get(geoHandleEB_);
    ebGeometry_ = geoHandleEB_.product();
    //---get gen particles---
    Event.getByLabel("genParticles", genPartHandle_);
    //---get gen jets---
    Event.getByLabel(genJetsInputTag_, genJetsHandle_);
    //---get reco jets---    
    Event.getByLabel(recoJetsInputTag_, recoJetsHandle_);

    //---get signal gen vertex time---
    const SimVertex* genSignalVtx=NULL;
    Event.getByLabel("g4SimHits", genSigVtxHandle_);
    if(genSigVtxHandle_.product()->size() == 0 || genSigVtxHandle_.product()->at(0).vertexId() != 0)
        return;
    genSignalVtx = &genSigVtxHandle_.product()->at(0);
    //---get all the generated vtxs---
    Event.getByLabel(genVtxInputTag_, genVtxsHandle_);
    genVtxsMap_.clear();
    genVtxsMap_[0] = genSignalVtx;
    FindGenVtxs();
    //---get reco primary vtxs---
    VertexWithFT* recoSignalVtx=NULL;
    Event.getByLabel("offlinePrimaryVerticesWithBS", recoVtxHandle_);
    recoVtxCollection_.clear();
    for(unsigned int iVtx=0; iVtx<recoVtxHandle_.product()->size(); ++iVtx)
    {
        if(recoVtxHandle_.product()->at(iVtx).isValid() &&
           !recoVtxHandle_.product()->at(iVtx).isFake())
            recoVtxCollection_.push_back(VertexWithFT(&recoVtxHandle_.product()->at(iVtx)));
            // &&
           // recoVtxHandle_.product()->at(iVtx).chi2()/recoVtxHandle_.product()->at(iVtx).ndof() < 7 &&
           // recoVtxHandle_.product()->at(iVtx).chi2()/recoVtxHandle_.product()->at(iVtx).ndof() > 0.1)
    }
    if(recoVtxCollection_.size() == 0)
        return;

    //---get EK detailed time RecHits---
    Event.getByLabel(edm::InputTag("ecalDetailedTimeRecHit", "EcalRecHitsEK", "RECO"),
                      recSortEK_);
    if(!recSortEK_.isValid())
        return;
    vector<EcalRecHit>* recVectEK = (vector<EcalRecHit>*)recSortEK_.product();
    //---get EK detailed time RecHits---
    Event.getByLabel(edm::InputTag("ecalDetailedTimeRecHit", "EcalRecHitsEB", "RECO"),
                      recSortEB_);
    if(!recSortEB_.isValid())
        return;
    //vector<EcalRecHit>* recVectEB = (vector<EcalRecHit>*)recSortEB_.product();

    //---convert all particles---
    Event.getByLabel("particleFlow", candHandle_);
    PFCandidateWithFT particle;
    particlesCollection_.clear();
    for(unsigned int iCand=0; iCand<candHandle_.product()->size(); ++iCand)
    {
        // if(!candHandle_.product()->at(iCand).superClusterRef().isNull() &&
        //    fabs(candHandle_.product()->at(iCand).superClusterRef().get()->eta())<1.47)
        //     particle = PFCandidateWithFT(&candHandle_.product()->at(iCand), recVectEB,
        //                                  ebGeometry_, magField_, genSignalVtx, recoSignalVtx);            
        // else
        particle = PFCandidateWithFT(&candHandle_.product()->at(iCand), recVectEK,
                                     skGeometry_, magField_, genSignalVtx, recoSignalVtx);
        particlesCollection_.push_back(particle);
    }

    //---search for seeds particles---
    vector<PFCandidateWithFT*> chargedRefs;
    vector<PFCandidateWithFT*> neutralRefs;
    for(unsigned int iPart=0; iPart<particlesCollection_.size(); ++iPart)
    {
        PFCandidateWithFT* particleRef = &particlesCollection_.at(iPart);
        particleRef->SetMVAComputer(mvaComputer_);
        if(particleRef->particleId() < 4)
        {
            FindVtxSeedParticle(particleRef);
            if(particleRef->hasTime() && particleRef->GetRecoVtx() &&
               (!particleRef->GetRecoVtx()->hasSeed() ||
                particleRef->GetRecoVtx()->GetSeedRef()->pt() < particleRef->pt()))
            {
                //---store the old seed in the particles collection
                //---redundant since the particles collection is pt ordered
                if(particleRef->GetRecoVtx()->hasSeed())
                    chargedRefs.push_back(particleRef->GetRecoVtx()->GetSeedRef());
                //---set the new seed
                particleRef->GetRecoVtx()->SetSeed(particleRef);
            }
            else
                chargedRefs.push_back(particleRef);
        }
        else 
            neutralRefs.push_back(particleRef);        
    }

    //---link the remaining charged particles to vtxs---
    AssignChargedToVtxs(&chargedRefs);
    //---link the neutral particles to vtxs---
    AssignNeutralToVtxs(&neutralRefs);
    //---sort the vtxs by sumpt2---
    // sort(recoVtxCollection_.begin(), recoVtxCollection_.end());
    // reverse(recoVtxCollection_.begin(), recoVtxCollection_.end());
    //---match gen and reco vtxs---
    MatchGenVtx();
    //---compute vtxs times and fill the output tree---
    if(saveVertices_)
        ProcessVertices();
    else if(saveParticles_)
    {
        vector<PFCandidateWithFT*> allParticlesRefs;
        for(unsigned int iPart=0; iPart<particlesCollection_.size(); ++iPart)
            allParticlesRefs.push_back(&particlesCollection_.at(iPart));

        ProcessParticles(&allParticlesRefs, -1);
    }

    //ProcessJets(recVectEK);
    ComputeSumEt();
    return;
}

//**********Utils*************************************************************************

int RecoFastTiming::FindGenVtxs()
{
    // for(unsigned int iGen=0; iGen<genVtxsHandle_.product()->size(); ++iGen)
    // {
    //     TrackingVertex* pInteractionVertex=genVtxsHandle_.product()->at(iGen);
    //     while( !pInteractionVertex->sourceTracks().empty() )
    //     {
    //         pInteractionVertex=pInteractionVertex->sourceTracks().front().parentVertex();
    //     }
    // }
        
    for(unsigned int iGen=0; iGen<genVtxsHandle_.product()->size(); ++iGen)
    {
        genVtxsMap_[iGen+1] = &genVtxsHandle_.product()->at(iGen);
        // if(genVtxsHandle_.product()->getPileups().at(iGen)->vertexId()==0)
        //     genVtxsMap_[genVtxsHandle_.product()->getPileups().at(iGen)->eventId().event()+1]=
        //         genVtxsHandle_.product()->getPileups().at(iGen);
    }
    int n_merged=0;
    for(map<int, const SimVertex*>::iterator itG=++genVtxsMap_.begin(); itG != genVtxsMap_.end(); ++itG)
    {
        map<int, const SimVertex*>::iterator itN = itG;
        --itN;
        if(fabs(itG->second->position().z() - itN->second->position().z()) < 0.05)
            ++n_merged;
    }
    return n_merged;
}

void RecoFastTiming::MatchGenVtx()
{
    for(vector<VertexWithFT>::iterator itR = recoVtxCollection_.begin(); itR != recoVtxCollection_.end(); ++itR)
    {
        if(!itR->hasSeed())
            continue;
        float min_Zdist=100;
        float min_Tdist=100;
        int matchedGenId=-1;
        int matchedZGenId=-1;
        int matchedTGenId=-1;
        for(map<int, const SimVertex*>::iterator itG=genVtxsMap_.begin(); itG != genVtxsMap_.end(); ++itG)
        {
            float tmp_Zdist = fabs(itR->z() - itG->second->position().z());
            float tmp_Tdist = fabs(itR->GetSeedRef()->GetVtxTime(tRes_) - itG->second->position().t()*1E9);
            if(tmp_Zdist < min_Zdist && tmp_Zdist<0.1)
            {
                min_Zdist = tmp_Zdist;
                //itR->SetGenVtxRef(itG->second->position(), itG->second->eventId().event());
                matchedZGenId = itG->first;
            }
            if(tmp_Tdist < min_Tdist && tmp_Tdist<0.1)
            {
                min_Tdist = tmp_Tdist;
                //itR->SetGenVtxRef(itG->second->position(), itG->second->eventId().event());
                matchedTGenId = itG->first;
            }
        }
        if(matchedZGenId != -1 && matchedTGenId != -1 && matchedTGenId!=matchedZGenId)
        {
            // if(fabs(genVtxsMap_[matchedZGenId]->position().z()-
            //         genVtxsMap_[matchedTGenId]->position().z()) < 0.05)
            // {
            //     cout << "merged found" << endl;
            //     matchedGenId = matchedTGenId;
            // }
            // else
                matchedGenId = matchedZGenId;
        }
        else
            matchedGenId = matchedZGenId==-1 ? matchedTGenId : matchedZGenId;
        if(matchedGenId != -1)
        {
            itR->SetGenVtxRef(genVtxsMap_[matchedGenId]->position(), matchedGenId);            
            genVtxsMap_.erase(matchedGenId);
        }
    }
}

int RecoFastTiming::FindVtxSeedParticle(PFCandidateWithFT* particle)
{
    if(!particle->GetTrack())
        return -1;
    
    int goodVtx=-1;
    float dz_min = 100;    
    for(unsigned int iVtx=0; iVtx<recoVtxCollection_.size(); ++iVtx)
    {            
        float dz_tmp = fabs(particle->GetTrack()->dz(recoVtxCollection_.at(iVtx).position()));
        if(dz_tmp < dzCut_ && dz_tmp < dz_min)
        {
            dz_min = dz_tmp;
            goodVtx = iVtx;
        }
    }
    if(goodVtx != -1)
        particle->SetRecoVtx(&recoVtxCollection_.at(goodVtx));
    
    return goodVtx;
}

void RecoFastTiming::AssignChargedToVtxs(vector<PFCandidateWithFT*>* charged_cand)
{
    vector<PFCandidateWithFT*>::iterator it;
    while(charged_cand->size() != 0)
    {
        it=charged_cand->end();
        --it;
        if(!(*it)->GetTrack())
        {
            charged_cand->pop_back();
            continue;
        }
        int goodVtx=-1;
        float dz_min=dzCut_;//, dt_min=10;
        for(unsigned int iVtx=0; iVtx<recoVtxCollection_.size(); ++iVtx)
        {
            float dz_tmp = fabs((*it)->GetTrack()->dz(recoVtxCollection_.at(iVtx).position()));
            if(/*recoVtxCollection_.at(iVtx).hasSeed() &&*/  dz_tmp < dzCut_)
            {
                (*it)->SetRecoVtx(&recoVtxCollection_.at(iVtx));
                // float dt_tmp=0;
                // dt_tmp = fabs((*it)->GetVtxTime(tRes_) -
                //               recoVtxCollection_.at(iVtx).GetSeedRef()->GetVtxTime(tRes_));
                if(dz_tmp < dz_min) //&& dt_tmp < tRes_*2)// && dt_tmp < dt_min)
                {
                    goodVtx=iVtx;
                    dz_min=dz_tmp;
                    //dt_min=dt_tmp;
                }
            }
        }
        if(goodVtx != -1)
        {
            recoVtxCollection_.at(goodVtx).AddParticle(*it);
            (*it)->SetRecoVtx(&recoVtxCollection_.at(goodVtx));
        }
        charged_cand->pop_back();        
    }
}

void RecoFastTiming::AssignNeutralToVtxs(vector<PFCandidateWithFT*>* neutral_cand)
{
    vector<PFCandidateWithFT*>::iterator it;
    while(neutral_cand->size() != 0)
    {
        it=neutral_cand->end();
        --it;
        if(!(*it)->hasTime())
        {
            neutral_cand->pop_back();
            continue;
        }
        int goodVtx=-1;
        float dt_min=10;
        for(unsigned int iVtx=0; iVtx<recoVtxCollection_.size(); ++iVtx)
        {
            if(recoVtxCollection_.at(iVtx).hasSeed() &&
               fabs(recoVtxCollection_.at(iVtx).GetSeedRef()->GetVtxTime(tRes_, false)) < 0.5)
            {
                (*it)->SetRecoVtx(&recoVtxCollection_.at(iVtx));
                float dt_tmp=0;
                dt_tmp = fabs((*it)->GetVtxTime(tRes_) -
                              recoVtxCollection_.at(iVtx).ComputeTime(1, ptCut_, pz2Cut_, tRes_));
                if(dt_tmp < dt_min && dt_tmp<tRes_*2)
                {                    
                    goodVtx=iVtx;
                    dt_min=dt_tmp;
                }
            }
        }
        if(goodVtx != -1)
        {
            recoVtxCollection_.at(goodVtx).AddParticle(*it);
            (*it)->SetRecoVtx(&recoVtxCollection_.at(goodVtx));
        }
        neutral_cand->pop_back();        
    }
}

void RecoFastTiming::ProcessVertices()
{
    for(unsigned int iVtx=0; iVtx<recoVtxCollection_.size(); ++iVtx)
    {
        vector<VertexWithFT>::iterator it = recoVtxCollection_.begin();
        advance(it, iVtx);
        //---fix references after sort
        it->FixVtxRefs();
        outFile_->verticesTree.event_n = iEvent_;
        outFile_->verticesTree.reco_vtx_index = iVtx;
        outFile_->verticesTree.reco_vtx_ndof = it->GetRecoVtxRef()->ndof();
        outFile_->verticesTree.reco_vtx_chi2 = it->GetRecoVtxRef()->chi2();
        outFile_->verticesTree.reco_vtx_sumpt2 = it->sumPtSquared(dzCut_, ptCut_);
        outFile_->verticesTree.reco_vtx_seed_pt = it->GetSeedRef() ?
            it->GetSeedRef()->pt() : -1;
        outFile_->verticesTree.reco_vtx_seed_t = it->GetSeedRef() && it->GetSeedRef()->hasTime() ?
            it->GetSeedRef()->GetVtxTime(tRes_, false) : -100;
        outFile_->verticesTree.reco_vtx_z = it->z();
        outFile_->verticesTree.reco_vtx_t = it->ComputeTime(0, ptCut_, pz2Cut_, tRes_);        
        outFile_->verticesTree.reco_vtx_n_part = it->GetNPart();
        outFile_->verticesTree.reco_vtx_cha_t = it->ComputeTime(1, ptCut_, pz2Cut_, tRes_);
        outFile_->verticesTree.reco_vtx_n_cha = it->GetNPart();
        outFile_->verticesTree.reco_vtx_neu_t = it->ComputeTime(2, ptCut_, pz2Cut_, tRes_);
        outFile_->verticesTree.reco_vtx_n_neu = it->GetNPart();
        if(it->GetGenVtxRef())
        {
            outFile_->verticesTree.gen_vtx_id = it->GetGenVtxId();
            outFile_->verticesTree.gen_vtx_x = it->GetGenVtxRef()->position().x();
            outFile_->verticesTree.gen_vtx_y = it->GetGenVtxRef()->position().y();
            outFile_->verticesTree.gen_vtx_z = it->GetGenVtxRef()->position().z();
            outFile_->verticesTree.gen_vtx_t = it->GetGenVtxRef()->position().t();
        }
        else
            outFile_->verticesTree.gen_vtx_id = -1;
        outFile_->verticesTree.Fill();
        if(saveParticles_)
        {
            vector<PFCandidateWithFT*> vtxParticles = it->GetParticles();
            ProcessParticles(&vtxParticles, iVtx);
        }
    }
}

void RecoFastTiming::ProcessParticles(vector<PFCandidateWithFT*>* particles, int iVtx)
{
    int index=-1;
    //---loop over all particles---
    for(vector<PFCandidateWithFT*>::iterator it=particles->begin(); it!=particles->end(); ++it)
    {
        ++index;
        //---fill output tree---
        if((*it)->hasTime())
        {            
            //---fill gen vtx infos 
            if((*it)->GetRecoVtx()->GetGenVtxRef())
            {
                outFile_->particlesTree.gen_vtx_z = (*it)->GetRecoVtx()->GetGenVtxRef()->position().z();
                outFile_->particlesTree.gen_vtx_t = (*it)->GetRecoVtx()->GetGenVtxRef()->position().t();
            }
            //---particle variables
            outFile_->particlesTree.particle_n = index;
            outFile_->particlesTree.particle_type = (*it)->particleId();
            outFile_->particlesTree.particle_p = (*it)->p();
            outFile_->particlesTree.particle_pz = (*it)->pz();
            outFile_->particlesTree.particle_pt = (*it)->pt();
            outFile_->particlesTree.particle_eta = (*it)->eta();
            outFile_->particlesTree.particle_phi = (*it)->phi();
            //---ecal/time variables
            outFile_->particlesTree.cluster_E = (*it)->GetPFCluster()->energy();
            outFile_->particlesTree.cluster_eta = (*it)->GetPFClusterPos().eta();
            outFile_->particlesTree.cluster_phi = (*it)->GetPFClusterPos().phi();
            outFile_->particlesTree.maxE_time = (*it)->GetRecHitTimeMaxE().first;
            outFile_->particlesTree.maxE_energy = (*it)->GetRecHitTimeMaxE().second;
            outFile_->particlesTree.all_time.clear();
            outFile_->particlesTree.all_energy.clear();
            //---vertex reco info
            outFile_->particlesTree.reco_vtx_index = iVtx;
            outFile_->particlesTree.reco_vtx_t = (*it)->GetVtxTime(tRes_, false);
            outFile_->particlesTree.reco_vtx_z = (*it)->GetRecoVtxPos().z();
            //---track info
            outFile_->particlesTree.track_length = (*it)->GetTrackLength();
            outFile_->particlesTree.track_length_helix = (*it)->GetPropagatedTrackLength();
            if((*it)->GetTrack())
            {
                //---only for charged
                outFile_->particlesTree.track_dz = (*it)->GetTrack()->dz((*it)->GetRecoVtx()->position());
                outFile_->particlesTree.track_dxy = (*it)->GetTrack()->dxy((*it)->GetRecoVtx()->position());            
                outFile_->particlesTree.trackCluster_dr = (*it)->GetDrTrackCluster();
            }
            vector<FTEcalRecHit>* recHits_vect=NULL;
            if(saveAllRecHits_)
                recHits_vect = (*it)->GetRecHits();
            if(recHits_vect && recHits_vect->size() != 0)
            {
                for(auto recHit : *recHits_vect)
                {
                    outFile_->particlesTree.all_time.push_back(recHit.time);
                    outFile_->particlesTree.all_energy.push_back(recHit.energy);
                }
            }
            outFile_->particlesTree.Fill();
        }
    }
}

void RecoFastTiming::ProcessJets(vector<EcalRecHit>* recVectEK)
{
    outFile_->jetsTree.gen_n = 0;
    outFile_->jetsTree.chs_n = 0;
    outFile_->jetsTree.tcut_n = 0;
    
    vector<JetWithFT> timeCorrJets;
    int countJ=0, countGJ=0;
    int goodJ1=-1, goodJ2=-1;
    int goodGJ1=-1, goodGJ2=-1;

    TLorentzVector j1,j2,gj1,gj2;

    //---GEN
    for(auto& gen_jet : *genJetsHandle_.product())
    {
        if(fabs(gen_jet.eta())>1.5 && fabs(gen_jet.eta())<3 && gen_jet.isJet())
        {
            if(goodGJ1 == -1)
                goodGJ1 = countGJ;
            else if(goodGJ2 == -1)
                goodGJ2 = countGJ;
        }
        ++countGJ;
    }
    if(goodGJ1!=-1 && genJetsHandle_.product()->at(goodGJ1).pt())
    {
        gj1 = TLorentzVector(genJetsHandle_.product()->at(goodGJ1).px(),
                             genJetsHandle_.product()->at(goodGJ1).py(),
                             genJetsHandle_.product()->at(goodGJ1).pz(),
                             genJetsHandle_.product()->at(goodGJ1).energy());
        outFile_->jetsTree.gen_j1_pt = gj1.Pt();
        outFile_->jetsTree.gen_j1_eta = gj1.Eta();
        outFile_->jetsTree.gen_j1_E = gj1.E();
        outFile_->jetsTree.gen_n = 1;
    }
    if(goodGJ2!=-1 && genJetsHandle_.product()->at(goodGJ2).pt())
    {
        gj2 = TLorentzVector(genJetsHandle_.product()->at(goodGJ2).px(),
                             genJetsHandle_.product()->at(goodGJ2).py(),
                             genJetsHandle_.product()->at(goodGJ2).pz(),
                             genJetsHandle_.product()->at(goodGJ2).energy());
        outFile_->jetsTree.gen_j2_pt = gj2.Pt();    
        outFile_->jetsTree.gen_j2_eta = gj2.Eta();    
        outFile_->jetsTree.gen_j2_E = gj2.E();
        outFile_->jetsTree.gen_jj_m = (gj1 + gj2).M();
        outFile_->jetsTree.gen_n = 2;
    }

    //---RECO CHS
    for(auto& chs_jet : *recoJetsHandle_.product())
    {
        if(fabs(chs_jet.eta())>1.5 && fabs(chs_jet.eta())<3 && chs_jet.isJet())
        {
            if(goodJ1 == -1)
                goodJ1 = countJ;
            else if(goodJ2 == -1)
                goodJ2 = countJ;
            JetWithFT timeJet(&chs_jet, &recoVtxCollection_[0], tRes_,
                              recVectEK, skGeometry_, magField_);
            timeCorrJets.push_back(timeJet);
        }
        ++countJ;
    }
    sort(timeCorrJets.begin(), timeCorrJets.end());
    reverse(timeCorrJets.begin(), timeCorrJets.end());

    if(goodJ1!=-1 && goodGJ1!=-1 && recoJetsHandle_.product()->at(goodJ1).pt()!=0)
    {
        j1 = TLorentzVector(recoJetsHandle_.product()->at(goodJ1).px(),
                            recoJetsHandle_.product()->at(goodJ1).py(),
                            recoJetsHandle_.product()->at(goodJ1).pz(),
                            recoJetsHandle_.product()->at(goodJ1).energy());
        outFile_->jetsTree.chs_j1_pt = j1.Pt();
        outFile_->jetsTree.chs_j1_eta = j1.Eta();
        outFile_->jetsTree.chs_j1_E = j1.E();
        outFile_->jetsTree.tcut_j1_dR = deltaR(j1.Eta(), j1.Phi(), gj1.Eta(), gj1.Phi());
        outFile_->jetsTree.chs_n = 1;
    }
    if(goodJ2!=-1 && goodGJ2!=-1 && recoJetsHandle_.product()->at(goodJ2).pt()!=0)
    {
        j2 = TLorentzVector(recoJetsHandle_.product()->at(goodJ2).px(),
                            recoJetsHandle_.product()->at(goodJ2).py(),
                            recoJetsHandle_.product()->at(goodJ2).pz(),
                            recoJetsHandle_.product()->at(goodJ2).energy());
        outFile_->jetsTree.chs_j2_pt = j2.Pt();    
        outFile_->jetsTree.chs_j2_eta = j2.Eta();    
        outFile_->jetsTree.chs_j2_E = j2.E();
        outFile_->jetsTree.tcut_j2_dR = deltaR(j2.Eta(), j2.Phi(), gj2.Eta(), gj2.Phi());
        outFile_->jetsTree.chs_jj_m = (j1 + j2).M();
        outFile_->jetsTree.chs_n = 2;
    }
    //---time CUT jets
    if(timeCorrJets.size() > 0 && goodGJ1!=-1 && timeCorrJets.at(0).GetCorrMomentum()->Pt()!=0)
    {
        outFile_->jetsTree.chs_j1_pt = timeCorrJets.at(0).GetCorrMomentum()->Pt();
        outFile_->jetsTree.chs_j1_eta = timeCorrJets.at(0).GetCorrMomentum()->Eta();
        outFile_->jetsTree.chs_j1_E = timeCorrJets.at(0).GetCorrMomentum()->E();
        outFile_->jetsTree.tcut_j1_dR = deltaR(outFile_->jetsTree.tcut_j1_eta,
                                               timeCorrJets.at(0).GetCorrMomentum()->Phi(),
                                               gj1.Eta(), gj1.Phi());
        outFile_->jetsTree.tcut_n = 1;
    }
    if(timeCorrJets.size() > 1 && goodGJ2!=-1 && timeCorrJets.at(1).GetCorrMomentum()->Pt()!=0)
    {
        outFile_->jetsTree.chs_j2_pt = timeCorrJets.at(0).GetCorrMomentum()->Pt();
        outFile_->jetsTree.chs_j2_eta = timeCorrJets.at(0).GetCorrMomentum()->Eta();
        outFile_->jetsTree.chs_j2_E = timeCorrJets.at(0).GetCorrMomentum()->E();
        outFile_->jetsTree.tcut_j2_dR = deltaR(outFile_->jetsTree.tcut_j2_eta,
                                               timeCorrJets.at(1).GetCorrMomentum()->Phi(),
                                               gj2.Eta(), gj2.Phi());
        outFile_->jetsTree.tcut_n = 2;
        outFile_->jetsTree.tcut_jj_m = (*timeCorrJets.at(0).GetCorrMomentum() +
                                        *timeCorrJets.at(1).GetCorrMomentum()).M();
    }
    outFile_->jetsTree.Fill();
}

void RecoFastTiming::ComputeSumEt()
{
    outFile_->globalTree.sumEt_nocut = 0;
    outFile_->globalTree.sumEt_t_cut = 0;
    outFile_->globalTree.sumEt_gen = 0;

    // if(!recoVtxCollection_[0].GetSeedRef()->hasTime() ||
    //    fabs(recoVtxCollection_[0].GetSeedRef()->GetVtxTime(tRes_, false)) > 0.8)
    //     return;
    //---sumEt
    for(auto& particle : particlesCollection_)
    {
        if(fabs(particle.eta()) > 1.5)
        {
            if(!recoVtxCollection_[0].GetSeedRef()->hasTime() ||
               fabs(recoVtxCollection_[0].GetSeedRef()->GetVtxTime(tRes_, false)) > 0.8)
            {
                if((particle.particleId() < 4 && particle.GetRecoVtx() == &recoVtxCollection_[0])
                   || particle.particleId() == 4)
                {
                    outFile_->globalTree.sumEt_nocut += particle.ecalEnergy();
                    outFile_->globalTree.sumEt_t_cut += particle.ecalEnergy();
                }
            }
            else
            {
                if(particle.particleId() < 4 && particle.GetRecoVtx() == &recoVtxCollection_[0])
                {
                    outFile_->globalTree.sumEt_nocut += particle.ecalEnergy();
                    outFile_->globalTree.sumEt_t_cut += particle.ecalEnergy();
                    if(particle.hasTime() && 
                       fabs(particle.GetVtxTime(tRes_) - recoVtxCollection_[0].GetSeedRef()->GetVtxTime(tRes_))>tRes_*2)
                        outFile_->globalTree.sumEt_t_cut -= particle.ecalEnergy();
                }
                if(particle.particleId() == 4)
                {
                    outFile_->globalTree.sumEt_nocut += particle.ecalEnergy();
                    if(particle.hasTime() && (particle.GetRecoVtx() == &recoVtxCollection_[0]
                                              || (particle.GetRecoVtx()==NULL &&
                                                  fabs(particle.GetVtxTime(tRes_) - recoVtxCollection_[0].GetSeedRef()->GetVtxTime(tRes_))>tRes_*2)))
                        outFile_->globalTree.sumEt_t_cut += particle.ecalEnergy();
                }
                // if(particle.particleId() >= 4)
                //     outFile_->globalTree.sumEt_nocut += particle.ecalEnergy();
                // if(particle.particleId() == 4 && particle.hasTime() && 
                //    fabs(particle.GetVtxTime(tRes_) - recoVtxCollection_[0].GetSeedRef()->GetVtxTime(tRes_))<tRes_*2)
                //     outFile_->globalTree.sumEt_t_cut += particle.ecalEnergy();
                // if(particle.particleId() > 4)
                //     outFile_->globalTree.sumEt_t_cut += particle.ecalEnergy();
            }
        }
    }
    for(auto& gen_part : *genPartHandle_.product())
    {
        if(gen_part.status() == 1 && fabs(gen_part.eta()) > 1.5 &&
           (fabs(gen_part.pdgId()) == 11 ||
            fabs(gen_part.pdgId()) == 13 ||
            fabs(gen_part.pdgId()) == 111 ||
            fabs(gen_part.pdgId()) == 211 ||
            fabs(gen_part.pdgId()) == 310 ||
            fabs(gen_part.pdgId()) == 321))
            outFile_->globalTree.sumEt_gen += gen_part.et();
    }
    outFile_->globalTree.Fill();
}   

//define this as a plugin
DEFINE_FWK_MODULE(RecoFastTiming);

