//
// Created by moriya on 7/24/19.
//

#include "ProtocolParty.h"

ProtocolParty::ProtocolParty(int argc, char* argv[]) : Protocol("ObliviousDictionary", argc, argv)
{

    partyId = stoi(this->getParser().getValueByKey(arguments, "partyID"));

    this->times = stoi(this->getParser().getValueByKey(arguments, "internalIterationsNumber"));
    this->hashSize = stoi(this->getParser().getValueByKey(arguments, "hashSize"));
    this->fieldSize = stoi(this->getParser().getValueByKey(arguments, "fieldSize"));
    fieldSizeBytes = fieldSize % 8 == 0 ? fieldSize/8 : fieldSize/8 + 1;
    int zeroBits = 8 - fieldSize % 8;
    unsigned char zeroMask = 0xff >> zeroBits;

    cout << "fieldSizeBytes: " << fieldSizeBytes << endl;
    cout << "zeroBits: " << zeroBits << endl;
    cout << "zero mask: " << (int)zeroMask << "  " << std::bitset<8>(zeroMask) << endl;

    gamma = 60;
    vector<string> subTaskNames{"Online"};
    timer = new Measurement(*this, subTaskNames);

    MPCCommunication comm;
    string partiesFile = this->getParser().getValueByKey(arguments, "partiesFile");

    reportStatistics = stoi(this->getParser().getValueByKey(arguments, "reportStatistics"));


    if(reportStatistics==0) {
    otherParty = comm.setCommunication(io_service, partyId, 2, partiesFile)[0];


        string tmp = "init times";
        //cout<<"before sending any data"<<endl;
        byte tmpBytes[20];
        if (otherParty->getID() < partyId) {
            otherParty->getChannel()->write(tmp);
            otherParty->getChannel()->read(tmpBytes, tmp.size());
        } else {
            otherParty->getChannel()->read(tmpBytes, tmp.size());
            otherParty->getChannel()->write(tmp);
        }
    }



    dic = new ObliviousDictionary(hashSize, fieldSize, gamma);
    dic->setReportStatstics(reportStatistics);

    hashSize = dic->getHashSize();
    tableRealSize = dic->getTableSize();
    numOTs = tableRealSize*2.4 + gamma;
    cout<<"after create dictionary. hashSize = "<<hashSize<<endl;
    cout<<"after create dictionary. tableRealSize = "<<tableRealSize<<endl;
    keys.resize(hashSize);
    vals.resize(hashSize*fieldSizeBytes);


    //    auto key = prg.generateKey(128);
    prg = PrgFromOpenSSLAES(hashSize*fieldSizeBytes*2);
    vector<byte> seed(128, 0);
    SecretKey key(seed, "");
    prg.setKey(key);

    for (int i=0; i < hashSize; i++){
        keys[i] = i;//prg.getRandom64();
    }
    prg.getPRGBytes(vals, 0, hashSize*fieldSizeBytes);

    cout << "masking the values so they will not have random in the padding bits" << endl;
    for (int i=0; i<hashSize; i++){
//        cout << "key = " << keys[i] << " val = ";
//        for (int j=0; j<fieldSizeBytes; j++){
//            cout<<(int)(vals[i*fieldSizeBytes + j])<<" " << std::bitset<8>(vals[i*fieldSizeBytes + j]) << " ";
//        }
//        cout<<endl;

        vals[(i+1)*fieldSizeBytes-1] = vals[(i+1)*fieldSizeBytes-1]  >> zeroBits;
//
//        cout << "key = " << keys[i] << " val = ";
//        for (int j=0; j<fieldSizeBytes; j++){
//            cout<<(int)(vals[i*fieldSizeBytes + j])<<" " << std::bitset<8>(vals[i*fieldSizeBytes + j]) << " ";
//        }
//        cout<<endl;
    }

    aes = EVP_CIPHER_CTX_new();
    const EVP_CIPHER* cipher = EVP_aes_128_ecb();

    vector<byte> fixedKey(128);
    prg.getPRGBytes(fixedKey, 0, 128);
    EVP_EncryptInit(aes, cipher, fixedKey.data(), NULL);

}

ProtocolParty::~ProtocolParty(){

    delete timer;
}

void ProtocolParty::run() {

    for (iteration=0; iteration<times; iteration++){

        auto t1start = high_resolution_clock::now();
        timer->startSubTask("Online", iteration);
        runOnline();
        timer->endSubTask("Online", iteration);

        auto t2end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(t2end-t1start).count();

        cout << "time in milliseconds for protocol: " << duration << endl;
    }


}

Receiver::Receiver(int argc, char *argv[]): ProtocolParty(argc, argv){

    dic->setKeysAndVals(keys, vals);

}

Receiver::~Receiver(){
    delete dic;
}

void Receiver::runOnline() {

    auto start = high_resolution_clock::now();
    auto t1 = high_resolution_clock::now();

    auto sigma = createDictionary();
    auto t2 = high_resolution_clock::now();

    auto duration = duration_cast<milliseconds>(t2-t1).count();
    cout << "createDictionary took in milliseconds: " << duration << endl;
//    checkVariables(sigma);
    t1 = high_resolution_clock::now();

    runOOS(sigma);
    t2 = high_resolution_clock::now();

    duration = duration_cast<milliseconds>(t2-t1).count();
    cout << "run OOS took in milliseconds: " << duration << endl;


    t1 = high_resolution_clock::now();

    computeXors();
    t2 = high_resolution_clock::now();

    duration = duration_cast<milliseconds>(t2-t1).count();
    cout << "computeXors took in milliseconds: " << duration << endl;

    t1 = high_resolution_clock::now();

    receiveSenderXors();
    t2 = high_resolution_clock::now();

    duration = duration_cast<milliseconds>(t2-t1).count();
    cout << "receiveXors took in milliseconds: " << duration << endl;
    auto end = high_resolution_clock::now();

    duration = duration_cast<milliseconds>(end - start).count();
    cout << "all protocol took in milliseconds: " << duration << endl;

}

vector<byte> Receiver::createDictionary(){
    dic->init();

    auto start = high_resolution_clock::now();
    auto t1 = high_resolution_clock::now();

    dic->fillTables();
    auto t2 = high_resolution_clock::now();

    auto duration = duration_cast<milliseconds>(t2-t1).count();
    cout << "fillTables took in milliseconds: " << duration << endl;

    t1 = high_resolution_clock::now();
    dic->peeling();

    t2 = high_resolution_clock::now();

    duration = duration_cast<milliseconds>(t2-t1).count();
    cout << "peeling took in milliseconds: " << duration << endl;

    t1 = high_resolution_clock::now();
    dic->generateExternalToolValues();
    t2 = high_resolution_clock::now();

    duration = duration_cast<milliseconds>(t2-t1).count();
    cout << "calc equations took in milliseconds: " << duration << endl;

    if(reportStatistics==0) {
        t1 = high_resolution_clock::now();
        dic->unpeeling();

        t2 = high_resolution_clock::now();

        duration = duration_cast<milliseconds>(t2 - t1).count();
        cout << "unpeeling took in milliseconds: " << duration << endl;
    } else{
        dic->unpeeling();
    }
    auto end = high_resolution_clock::now();

    duration = duration_cast<milliseconds>(end - start).count();
    cout << "all protocol took in milliseconds: " << duration << endl;

//        dic->checkOutput();


    return dic->getVariables();
}

void Receiver::runOOS(vector<byte> & sigma){

//    u64 setSize = hashSize;//1 << 5;
//    u64 numBin = hashSize*2.4; //2.4 * setSize;
//    u64 numOTs = sigma.size()/fieldSizeBytes;

    std::cout << "in runOOS() " << numOTs << "\n";

    std::string name = "n";
    IOService ios(0);
//    Session ep0(ios, "localhost", 1212, SessionMode::Server, name);
    Session ep1(ios, "localhost", 1212, SessionMode::Client, name);
    auto recvChl = ep1.addChannel(name, name);
//    auto sendChl = ep0.addChannel(name, name);

    LinearCode code;
    code.load(mx132by583, sizeof(mx132by583));

//    PrtyMOtSender sender;
    recv.configure(true, 40, 132);

    //Base OT - simulated
    PRNG prng0(_mm_set_epi32(4253465, 3434565, 234435, 23987045));
    u64 baseCount = recv.getBaseOTCount();
    std::vector<block> baseRecv(baseCount);
    std::vector<std::array<block, 2>> baseSend(baseCount);
    BitVector baseChoice(baseCount);
    baseChoice.randomize(prng0);
    prng0.get((u8*)baseSend.data()->data(), sizeof(block) * 2 * baseSend.size());
    for (u64 i = 0; i < baseCount; ++i)
    {
        baseRecv[i] = baseSend[i][baseChoice[i]];
    }

//    sender.setBaseOts(baseRecv, baseChoice);
    recv.setBaseOts(baseSend);

    //OT
    PRNG prng1(_mm_set_epi32(4253465, 3434565, 234435, 23987025));
    recv.init(numOTs, prng1, recvChl);

    for (u64 i = 0; i < numOTs; i += stepSize)
    {
//        cout<<"step size "<<i<<endl;
        auto curStepSize = std::min<u64>(stepSize, numOTs - i);
//        cout<<"curStepSize "<<curStepSize<<endl;
        for (u64 k = 0; k < curStepSize; ++k)
        {
            recv.otCorrection(i + k, &sigma[fieldSizeBytes*(k + i)]);
        }
        recv.sendCorrection(recvChl, curStepSize);
//        cout<<"after send correction"<<endl;
    }
}

void Receiver::computeXors(){

    u64 baseCount = recv.getBaseOTCount();
    int blockSize = baseCount/128;

    vector<block> output(blockSize);
    vector<byte> temp(blockSize*16);
    int size;

    xorsSet = unordered_set<uint64_t>(hashSize);

//    cout<<"baseCount = "<<baseCount<<endl;
//    cout<<"Xors:"<<endl;
    for (int i=0; i<hashSize; i++){

        auto indices = dic->dec(keys[i]);

        for (int j=0; j<blockSize; j++) {
            output[j] = _mm_xor_si128(*(recv.mT0.data(indices[0]) + j),*(recv.mT0.data(tableRealSize + indices[1]) + j));
        }

        for (int k=2; k<indices.size(); k++) {
            for (int j = 0; j < blockSize; j++) {
                output[j] = _mm_xor_si128(output[j], *(recv.mT0.data(2*tableRealSize + indices[k]) + j));
            }
        }


        EVP_EncryptUpdate(aes, temp.data(), &size, (byte*)output.data(), blockSize*16);
//cout<<"size = "<<size<<endl;
        xorsSet.insert(((uint64_t*)temp.data())[0]);

//        for (int j=0; j<20;j++){
//            cout<<(int)(((byte*)output.data())[j])<<" ";
//        }
//        cout<<endl;
//        cout<<((uint64_t*)temp.data())[0]<<endl;

    }


}

void Receiver::receiveSenderXors(){

    uint64_t size;
    otherParty->getChannel()->read((byte*)&size, 8);


    vector<uint64_t> senderOutputs(size);
    otherParty->getChannel()->read((byte*)senderOutputs.data(), size*8);

//    for (int i=0; i<size/blockSize; i++) {
//        for (int j=0; j<20; j++) {
//            cout<<(int)(((byte*)(senderOutputs.data() + i*blockSize))[j])<< " ";
//        }
//        cout<<endl;
//    }

    uint64_t count = 0;
    for (int i=0; i<hashSize; i++){
        if (xorsSet.find(senderOutputs[i]) != xorsSet.end()){
            count++;
//            cout<<"element "<<i<<" is in the intersection"<<endl;
        }
    }

    cout<<"found "<<count<<" matches"<<endl;


}



void Receiver::checkVariables(vector<byte> & variables){

//    u64 baseCount = recv.getBaseOTCount();
//    int blockSize = baseCount/128;
//
//    vector<vector<block>> outputs(keys.size(), vector<block>(blockSize));
//
//    cout<<"baseCount = "<<baseCount<<endl;
//    cout<<"Xors:"<<endl;

    bool error = false;
    vector<byte> check(fieldSizeBytes);
    for (int i=0; i<hashSize; i++){

        auto indices = dic->dec(keys[i]);

        for (int j=0; j<fieldSizeBytes; j++) {
            check[j] = variables[indices[0]*fieldSizeBytes + j] ^ variables[tableRealSize*fieldSizeBytes + indices[1]*fieldSizeBytes + j];
        }

        for (int k=2; k<indices.size(); k++) {
            for (int j=0; j<fieldSizeBytes; j++) {
                check[j] = check[j] ^ variables[2*tableRealSize*fieldSizeBytes + indices[k]*fieldSizeBytes + j];
            }
        }

        for (int j=0; j<fieldSizeBytes; j++) {
            if (check[j] != vals[i*fieldSizeBytes + j]){
                error = true;
                cout<<"error in check! xor of D values is not equal to val"<<endl;
            }
        }

    }

    if (error ==  false){
        cout<<"success!!! all D values equal to val"<<endl;
    }

}


Sender::Sender(int argc, char *argv[]) : ProtocolParty(argc, argv){
}

Sender::~Sender() {

}



void Sender::runOnline() {

    auto start = high_resolution_clock::now();
    auto t1 = high_resolution_clock::now();

//    dic->readData(otherParty);
    runOOS();
    auto t2 = high_resolution_clock::now();

    auto duration = duration_cast<milliseconds>(t2-t1).count();
    cout << "run OT took in milliseconds: " << duration << endl;

    t1 = high_resolution_clock::now();

    computeXors();
    t2 = high_resolution_clock::now();

    duration = duration_cast<milliseconds>(t2-t1).count();
    cout << "computeXors took in milliseconds: " << duration << endl;

    t1 = high_resolution_clock::now();

    sendXors();
    t2 = high_resolution_clock::now();

    duration = duration_cast<milliseconds>(t2-t1).count();
    cout << "sendXors took in milliseconds: " << duration << endl;
    auto end = high_resolution_clock::now();

    duration = duration_cast<milliseconds>(end-start).count();
    cout << "all protocol took in milliseconds: " << duration << endl;



}

void Sender::runOOS(){

//    u64 setSize = hashSize;//1 << 5;
//    u64 numBin = hashSize*2.4; //2.4 * setSize;
//    u64 numOTs = tableRealSize*2 + gamma;

    std::cout << "in runOOS() = " << numOTs << "\n";

    std::string name = "n";
    IOService ios(0);
    Session ep0(ios, "localhost", 1212, SessionMode::Server, name);
//    Session ep1(ios, "localhost", 1212, SessionMode::Client, name);
//    auto recvChl = ep1.addChannel(name, name);
    auto sendChl = ep0.addChannel(name, name);

    code.load(mx132by583, sizeof(mx132by583));


//    PrtyMOtReceiver recv;
    sender.configure(true, 40, 132);

    //Base OT - simulated
    PRNG prng0(_mm_set_epi32(4253465, 3434565, 234435, 23987045));
    u64 baseCount = sender.getBaseOTCount();

    cout<<"base count = "<<baseCount<<endl;
    std::vector<block> baseRecv(baseCount);
    std::vector<std::array<block, 2>> baseSend(baseCount);
    baseChoice.resize(baseCount);
    baseChoice.randomize(prng0);
    prng0.get((u8*)baseSend.data()->data(), sizeof(block) * 2 * baseSend.size());
    for (u64 i = 0; i < baseCount; ++i)
    {
        baseRecv[i] = baseSend[i][baseChoice[i]];
    }

    sender.setBaseOts(baseRecv, baseChoice);
//    recv.setBaseOts(baseSend);

    //OT
    sender.init(numOTs, prng0, sendChl);

    // Get the random OT messages
    for (u64 i = 0; i < numOTs; i += stepSize)
    {
//        cout<<"step size "<<i<<endl;
        auto curStepSize = std::min<u64>(stepSize, numOTs - i);
//        cout<<"curStepSize "<<curStepSize<<endl;
        sender.recvCorrection(sendChl, curStepSize);
        for (u64 k = 0; k < curStepSize; ++k)
        {
            sender.otCorrection(i + k);
        }
//        cout<<"after correction"<<endl;
    }

}

void Sender::computeXors(){

    u64 baseCount = sender.getBaseOTCount();
    int blockSize = baseCount/128;
    vector<block> output(blockSize);

    xors.resize(hashSize);

    vector<byte> temp(blockSize*16);
    int size;

    for (int i=0; i<hashSize; i++){

        auto indices = dic->dec(keys[i]);

        for (int j=0; j<blockSize; j++) {
            output[j] = _mm_xor_si128(*(sender.mT.data(indices[0]) + j),*(sender.mT.data(tableRealSize + indices[1]) + j));
        }

        for (int k=2; k<indices.size(); k++) {
            for (int j = 0; j < blockSize; j++) {
                output[j] = _mm_xor_si128(output[j], *(sender.mT.data(2*tableRealSize + indices[k]) + j));
            }
        }

        std::array<block, 10> codeword = { ZeroBlock, ZeroBlock, ZeroBlock, ZeroBlock, ZeroBlock, ZeroBlock, ZeroBlock, ZeroBlock, ZeroBlock, ZeroBlock };
        memcpy(codeword.data(), vals.data() + i*fieldSizeBytes, fieldSizeBytes);
        code.encode((u8*)codeword.data(), (u8*)codeword.data());

        for (int j = 0; j < blockSize; j++) {
            codeword[j] = _mm_and_si128(codeword[j], ((block*)baseChoice.data())[j]);
        }

        for (int j=0; j<blockSize; j++) {
            output[j] = _mm_xor_si128(output[j], codeword[j]);
        }

        EVP_EncryptUpdate(aes, temp.data(), &size, (byte*)output.data(), blockSize*16);

        xors[i] = ((uint64_t*)temp.data())[0];
//        for (int j=0; j<20;j++){
//            cout<<(int)(((byte*)output.data())[j])<<" ";
//        }
//        cout<<endl;
//
//        cout<<xors[i]<<endl;
    }


}

void Sender::sendXors(){

    int64_t size = xors.size();

    otherParty->getChannel()->write((byte*)&size, 8);
    otherParty->getChannel()->write((byte*)xors.data(), size*8);
}
//
//void Receiver::runOT(vector<byte> & sigma){
//
//
//    setThreadName("Sender");
//    PRNG prng0(_mm_set_epi32(4253465, 3434565, 234435, 23987045));
//    PRNG prng1(_mm_set_epi32(4253465, 3434565, 234435, 23987025));
//
//    u64 setSize = 1 << 5;
//    u64 numBin = 2.4 * setSize;
////    u64 sigma = 40;
//
//    std::cout << "input_size = " << setSize << "\n";
//    std::cout << "bin_size = " << numBin << "\n";
//
//    std::vector<block> inputs(setSize);
//    prng0.get(inputs.data(), inputs.size());
//
//    std::vector<std::array<block, prty2SuperBlkSize>> yInputs(setSize), cuckooTables;
//
//    for (int idxItem = 0; idxItem < inputs.size(); idxItem++)
//    {
//        for (int k = 0; k < prty2SuperBlkSize; k++)
//            yInputs[idxItem][k] = inputs[idxItem];  // H1(x)
//    }
//
//    // Generate a cuckoo table
////    Cuckoo_encode(inputs, yInputs, cuckooTables, numBin, sigma);
//
//    //===================================================
//    //=========================OOS========================
//    //===================================================
//
//    cuckooTables.resize(numBin);
//    u64 numOTs = cuckooTables.size();
//    std::cout << "OT = " << numOTs << "\n";
//
//    std::string name = "n";
//    IOService ios(0);
//    Session ep0(ios, "localhost", 1212, SessionMode::Server, name);
//    Session ep1(ios, "localhost", 1212, SessionMode::Client, name);
//    auto recvChl = ep1.addChannel(name, name);
//    auto sendChl = ep0.addChannel(name, name);
//
//
//    LinearCode code;
//    code.load(mx132by583, sizeof(mx132by583));
//
//    PrtyMOtSender sender;
//    PrtyMOtReceiver recv;
//
//    sender.configure(true, 40, 132);
//    recv.configure(true, 40, 132);
//    u64 baseCount = sender.getBaseOTCount();
//    //u64 codeSize = (baseCount + 127) / 128;
//
//    //Base OT
//    std::vector<block> baseRecv(baseCount);
//    std::vector<std::array<block, 2>> baseSend(baseCount);
//    BitVector baseChoice(baseCount);
//    baseChoice.randomize(prng0);
//
//    prng0.get((u8*)baseSend.data()->data(), sizeof(block) * 2 * baseSend.size());
//    for (u64 i = 0; i < baseCount; ++i)
//    {
//        baseRecv[i] = baseSend[i][baseChoice[i]];
//    }
//
//    sender.setBaseOts(baseRecv, baseChoice);
//    recv.setBaseOts(baseSend);
//
//    std::vector<block> oosEncoding1(numOTs), oosEncoding2(numOTs);
//
//    // perform the init on each of the classes. should be performed concurrently
//    auto thrd = std::thread([&]() {
//        setThreadName("Sender");
//        sender.init(numOTs, prng0, sendChl);
//
//        // Get the random OT messages
//        for (u64 i = 0; i < numOTs; i += stepSize)
//        {
//            auto curStepSize = std::min<u64>(stepSize, numOTs - i);
//            sender.recvCorrection(sendChl, curStepSize);
//            for (u64 k = 0; k < curStepSize; ++k)
//            {
//                sender.otCorrection(i + k);
//            }
//        }
//    });
//
//
//    recv.init(numOTs, prng1, recvChl);
//    for (u64 i = 0; i < numOTs; i += stepSize)
//    {
//        auto curStepSize = std::min<u64>(stepSize, numOTs - i);
//        for (u64 k = 0; k < curStepSize; ++k)
//        {
//            recv.otCorrection(i + k, &cuckooTables[k + i]);
//
//        }
//
//        recv.sendCorrection(recvChl, curStepSize);
//    }
//
//    thrd.join();
//
//    // Check OOS
//    for (u64 i = 0; i < numOTs; i++)
//    {
//        // check that we do in fact get the same value
//        if (neq(oosEncoding1[i], oosEncoding2[i]))
//        {
//            std::cout << oosEncoding1[i] << " vs " << oosEncoding2[i] << "\n";
////            throw UnitTestFail("ot[" + ToString(i) + "] not equal " LOCATION);
//                cout<<"ot[" + ToString(i) + "] not equal "<<endl;
//        }
//    }
//
//    //=========================================================
//    //=====================compute PSI encoding============
//    std::vector<block> prtyEncoding1(inputs.size()), prtyEncoding2(inputs.size());
//
//    thrd = std::thread([&]() {
//        //=====Sender
//        Cuckoo_decode(inputs, sender.mQx, sender.mT, numBin, sigma); //geting Decode(Q,x)
//        std::cout << sender.mQx[0][0] << " sender.mQx\n";
//
//
//        for (u64 i = 0; i < inputs.size(); i += stepSize)
//        {
//            auto curStepSize = std::min<u64>(stepSize, inputs.size() - i);
//            for (u64 k = 0; k < curStepSize; ++k)
//            {
//                //compute prtyEncoding1= H2(x, Decode(Q,x) +C(H1(x))*s)
//                sender.encode_prty(i + k, &yInputs[k + i], (u8*)& prtyEncoding1[k + i], sizeof(block));
//            }
//        }
//    });
//
//    //==========receiver
//    Cuckoo_decode(inputs, recv.mRy, recv.mT0, numBin, sigma); //Decode(R,y)
//    std::cout << recv.mRy[0][0] << " recv.mRy\n";
//
//
//    for (u64 i = 0; i < inputs.size(); i += stepSize)
//    {
//        auto curStepSize = std::min<u64>(stepSize, inputs.size() - i);
//        for (u64 k = 0; k < curStepSize; ++k)
//        {
//            // compute prtyEncoding1=H2(y, Decode(R,y))
//            recv.encode_prty(i + k, &inputs[k + i], (u8*)& prtyEncoding2[k + i], sizeof(block));
//        }
//    }
////    thrd.join();
//
//    // Check PRTY encoding
//    for (u64 i = 0; i < inputs.size(); i++)
//    {
//        // check that we do in fact get the same value
//        if (neq(prtyEncoding1[i], prtyEncoding2[i]))
//        {
//            std::cout << i << ": " << prtyEncoding1[i] << " vs " << prtyEncoding2[i] << "\n";
////				throw UnitTestFail("prty[" + ToString(i) + "]  not equal " LOCATION);
//        }
//    }

//    cout<<"in runOT receiver"<<endl;
//
//    PRNG prng0(_mm_set_epi32(4253465, 3434565, 234435, 23987045));
//
//    u64 numOTs = sigma.size()/fieldSizeBytes;
//
//    OosNcoOtReceiver* recv = new OosNcoOtReceiver;
//    recv->configure(true, 40, fieldSize);
//
//    std::string name = "n";
//    IOService ios(0);
//    Session ep1(ios, "localhost", 1212, SessionMode::Client, name);
//    auto recvChl = ep1.addChannel(name, name);
//
////    setBaseOts(sender, recv, sendChl, recvChl);
//    u64 baseCount = recv->getBaseOTCount();
//
//    std::vector<std::array<block, 2>> baseSend(baseCount);
//    prng0.get((u8*)baseSend.data()->data(), sizeof(block) * 2 * baseSend.size());
//
//    osuCrypto::NaorPinkas base;
//    base.send(baseSend, prng0, recvChl, 2);
//    recv->setBaseOts(baseSend, prng0, recvChl);
//
//    auto messageCount = 1ull << fieldSize;
////    Matrix<block> sendMessage(numOTs, messageCount);
//    std::vector<block> recvMessage(numOTs);
//
//    std::vector<u64> choices(numOTs);
//    for (u64 i = 0; i < choices.size(); ++i)
//    {
////        choices[i] = *(u64*)(sigma.data() + i*fieldSizeBytes);
//        choices[i] = i*1000;
//    }
//
//    span<u64> temp(choices.data(), numOTs);
//    span<block> tempmsg(recvMessage.data(), numOTs);
//    recv->receiveChosen(messageCount, tempmsg, temp, prng0, recvChl);

//    setThreadName("Sender");
//
//    PRNG prng0(_mm_set_epi32(4253465, 3434565, 234435, 23987045));
//    PRNG prng1(_mm_set_epi32(4253465, 3434565, 234435, 23987025));
//
//    u64 numOTs = 128 * 2;
//
//
//    std::string name = "n";
//    IOService ios(0);
//    Session ep0(ios, "localhost", 1212, SessionMode::Server, name);
//    Session ep1(ios, "localhost", 1212, SessionMode::Client, name);
//    auto recvChl = ep1.addChannel(name, name);
//    auto sendChl = ep0.addChannel(name, name);
//
//
//    OosNcoOtSender sender;
//    OosNcoOtReceiver recv;
//
//    sender.configure(true, 40, 76);
//    recv.configure(true, 40, 76);
//
//    if (1)
//    {
//        setBaseOts(sender, recv, sendChl, recvChl);
//        //for (u64 i = 0; i < sender.mBaseChoiceBits.size(); ++i)
//        //{
//        //    auto b = sender.mBaseChoiceBits[i];
//        //    if (neq(sender.mGens[i].getSeed(), recv.mGens[i][b].getSeed()))
//        //        throw RTE_LOC;
//        //}
//    }
//    else
//    {
//        u64 baseCount = sender.getBaseOTCount();
//        //u64 codeSize = (baseCount + 127) / 128;
//
//        std::vector<block> baseRecv(baseCount);
//        std::vector<std::array<block, 2>> baseSend(baseCount);
//        BitVector baseChoice(baseCount);
//        baseChoice.randomize(prng0);
//
//        prng0.get((u8*)baseSend.data()->data(), sizeof(block) * 2 * baseSend.size());
//        for (u64 i = 0; i < baseCount; ++i)
//        {
//            baseRecv[i] = baseSend[i][baseChoice[i]];
//        }
//
//        auto a = std::async([&]() {sender.setBaseOts(baseRecv, baseChoice, sendChl); });
//        recv.setBaseOts(baseSend, prng0, recvChl);
//        a.get();
//    }
//
//cout<<"after setBaseOts"<<endl;
//    testNco(sender, numOTs, prng0, sendChl, recv, prng1, recvChl);
//cout<<"after test"<<endl;
////    auto v = std::async([&] {
////        recv.check(recvChl, toBlock(322334));
////    });
////
////    try {
////        sender.check(sendChl,toBlock(324));
////    }
////    catch (...)
////    {
////        //sendChl.mBase->mLog;
////    }
////    v.get();
////
//    auto sender2 = sender.split();
//    auto recv2 = recv.split();
//
//    testNco(*sender2, numOTs, prng0, sendChl, *recv2, prng1, recvChl);
//    cout<<"after test 2"<<endl;


//}


void Sender::runOT(){
//    cout<<"in runOT sender"<<endl;
//    PRNG prng0(_mm_set_epi32(4253465, 3434565, 234435, 23987025));
//
//    u64 numOTs = 314;
//
//    OosNcoOtSender sender;
//    sender.configure(true, 40, fieldSize);
//
//    std::string name = "n";
//    IOService ios(0);
//    Session ep0(ios, "localhost", 1212, SessionMode::Server, name);
//    auto sendChl = ep0.addChannel(name, name);
//
////    setBaseOts(sender, recv, sendChl, recvChl);
//    u64 baseCount = sender.getBaseOTCount();
//
//    std::vector<block> baseRecv(baseCount);
//    BitVector baseChoice(baseCount);
//    baseChoice.randomize(prng0);
//
//    NaorPinkas base;
//    base.receive(baseChoice, baseRecv,prng0, sendChl, 2);
//
//    sender.setBaseOts(prng0, sendChl);
//
//
//    auto messageCount = 1ull << fieldSize;
//    Matrix<block> sendMessage(numOTs, messageCount);
//
//    prng0.get(sendMessage.data(), sendMessage.size());
//
//
//    sender.sendChosen(sendMessage, prng0, sendChl);

//    for (u64 i = 0; i < choices.size(); ++i)
//    {
//        if (neq(recvMessage[i], sendMessage(i, choices[i])))
//            cout<<"bad message"<<endl;
//    }
}
