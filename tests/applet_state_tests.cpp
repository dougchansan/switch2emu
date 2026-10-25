#include "machine.hpp"
#include "ipc.hpp"
#include <iostream>
static uint64_t get(const Ipc::Buffer& b,size_t at,size_t size=4)
{ uint64_t v=0;for(size_t i=0;i<size;++i) v|=uint64_t(b[at+i])<<(8*i);return v; }
static void put(Ipc::Buffer& b,size_t at,uint64_t v,size_t size=4)
{ for(size_t i=0;i<size;++i) b[at+i]=uint8_t(v>>(8*i)); }
static Ipc::Buffer snapshot(const Memory& ram,uint64_t at)
{ Ipc::Buffer b{};for(size_t i=0;i<b.size();++i) b[i]=ram.read8(at+i);return b; }
int main(int argc,char** argv)
{
    if(argc!=2) return 2;
    unsigned failures=0;
    auto check=[&](bool ok,const char* text) { if(!ok) { ++failures;std::cerr<<"FAIL: "<<text<<'\n'; } };
    Ipc::Buffer registration{},lookup{},convert{},openProxy{},window{},common{},identity{},event{},focus{},acquire{};
    Machine sdk(false,true,true);if(!sdk.load(argv[1])) return 2;const auto tls=sdk.process().mainThread().localRegion;
    for(unsigned i=0;i<100000;++i) {
        const auto op=sdk.ram.fetch32(sdk.cpu.getPC());const bool ipc=(op&0xFFE0001F)==0xD4000001 && ((op>>5)&0xFFFF)==0x21;
        Ipc::Buffer b{};if(ipc) b=snapshot(sdk.ram,tls);
        const auto step=sdk.run(1);
        if(ipc) {
            if(get(b,0)==5 && get(b,24)==0) convert=b;
            else if(get(b,0)==4) {
                const bool pid=get(b,4)&0x80000000U;const size_t cmif=pid?32:16;
                if(get(b,cmif)==0x49434653) {
                    if(pid && get(b,cmif+8)==0) registration=b;
                    if(get(b,cmif+8)==1 && std::string(reinterpret_cast<const char*>(b.data()+cmif+16),8)=="appletOE") lookup=b;
                } else {
                    Ipc::DomainRequest d;
                    if(!Ipc::parseDomain(b,d) && !d.close) {
                        if(d.object==1) openProxy=b;
                        if(d.object==2 && d.command==2) window=b;
                        if(d.object==2 && d.command==0) common=b;
                        if(d.object==7 && d.command==1) identity=b;
                        if(d.object==7 && d.command==10) {
                            acquire=b;
                            check(sdk.cpu.readRegister(0)==0 && sdk.ram.read32(tls+40)==0 &&
                                sdk.process().appletForegroundAcquired(),"original SDK acquires foreground rights before its next limitation");
                        }
                        if(d.object==5 && d.command==0) event=b;
                        if(d.object==5 && d.command==9) focus=b;
                    }
                }
            }
        }
        if(step.stop!=Machine::Stop::Budget) { check(step.stop==Machine::Stop::Exited && step.exitCode==16217,"SDK capture stops on genuine AM limitation");break; }
    }
    check(get(identity,40)==1 && get(event,40)==0 && get(focus,40)==9 && get(acquire,40)==10,
        "capture actual SDK state-method and foreground requests");
    Memory ram(Machine::RAM_SIZE);ProgramLoader::Image image;std::string error;Process p;
    if(!ProgramLoader::loadImage(ram,argv[1],image,error) || !p.initialize(ram,image,true,error,true)) return 2;
    const auto id=p.appletResourceId();check(id && p.appletActive(),"application identity is allocated at process initialization");
    Bus bus(ram);CPU cpu(bus);const auto ipc=p.mainThread().localRegion;
    const uint8_t port[]{'s','m',':',0};ram.writeBytes(0x43300,port,sizeof port);cpu.writeRegister(1,0x43300);p.dispatch(0x1F,cpu,bus);
    const auto manager=uint32_t(cpu.readRegister(1));
    auto send=[&](const Ipc::Buffer& b,uint32_t handle) { ram.writeBytes(ipc,b.data(),b.size());cpu.writeRegister(0,handle);p.dispatch(0x21,cpu,bus);return cpu.readRegister(0); };
    check(send(registration,manager)==0,"register host replay manager");
    auto makeWindow=[&]() {
        send(lookup,manager);const auto handle=ram.read32(ipc+12);send(convert,handle);send(openProxy,handle);
        send(window,handle);return std::pair{handle,ram.read32(ipc+48)};
    };
    const auto [root,windowId]=makeWindow();put(identity,20,windowId);
    put(acquire,20,windowId);
    check(!p.appletForegroundAcquired(),"a new process has not acquired foreground rights");
    auto wrongObject=acquire;put(wrongObject,20,1);
    check(send(wrongObject,root)==0 && ram.read32(ipc+40)==Ipc::UnknownCommand && !p.appletForegroundAcquired(),
        "foreground acquisition is restricted to the window controller");
    put(wrongObject,20,UINT32_MAX);
    check(send(wrongObject,root)==0 && ram.read32(ipc+40)==Ipc::InvalidObject && !p.appletForegroundAcquired(),
        "an invalid domain object cannot acquire rights");
    auto extraAcquire=acquire;put(extraAcquire,18,17,2);put(extraAcquire,4,13);
    check(send(extraAcquire,root)==0 && ram.read32(ipc+40)==Ipc::InvalidArguments && !p.appletForegroundAcquired(),
        "foreground acquisition rejects extra payload without changing state");
    auto pidAcquire=openProxy;put(pidAcquire,4,0x8000000C);put(pidAcquire,34,16,2);
    put(pidAcquire,36,windowId);put(pidAcquire,56,10);
    check(send(pidAcquire,root)==0 && ram.read32(ipc+40)==Ipc::InvalidArguments && !p.appletForegroundAcquired(),
        "foreground acquisition rejects a PID and copied handle");
    auto malformedAcquire=acquire;put(malformedAcquire,32,0);
    check(send(malformedAcquire,root)==Ipc::InvalidMessage && snapshot(ram,ipc)==malformedAcquire && !p.appletForegroundAcquired(),
        "a malformed foreground request leaves the request and state unchanged");
    ram.writeBytes(ipc,acquire.data(),acquire.size());ram.setPermissions(ipc,4096,Memory::Read);
    cpu.writeRegister(0,root);p.dispatch(0x21,cpu,bus);
    check(cpu.readRegister(0)==Process::InvalidPointer && snapshot(ram,ipc)==acquire && !p.appletForegroundAcquired(),
        "an unwritable foreground response cannot publish acquired rights");
    ram.setPermissions(ipc,4096,Memory::Read|Memory::Write);
    check(send(identity,root)==0 && ram.read32(ipc+40)==0 && ram.read64(ipc+48)==id && ram.read32(ipc+4)==14,
        "native resource query returns a u64 bound to the live process");
    auto malformed=identity;put(malformed,18,17,2);put(malformed,4,13);
    check(send(malformed,root)==0 && ram.read32(ipc+40)==Ipc::InvalidArguments,"resource query rejects extra payload");
    auto cloneRequest=convert;put(cloneRequest,24,2);send(cloneRequest,root);const auto clone=ram.read32(ipc+12);
    check(send(identity,clone)==0 && ram.read64(ipc+48)==id,"clone sees the same application resource identity");
    const auto [other,otherWindow]=makeWindow();auto otherIdentity=identity;put(otherIdentity,20,otherWindow);
    check(send(otherIdentity,other)==0 && ram.read64(ipc+48)==id,"independent applet connections bind the same process identity");
    send(common,root);const auto commonId=ram.read32(ipc+48);put(event,20,commonId);put(focus,20,commonId);
    check(send(event,root)==0 && ram.read32(ipc+8)==2,"message event is transmitted by copied handle");const auto firstEvent=ram.read32(ipc+12);
    check(send(event,clone)==0,"copy same message event via clone");const auto secondEvent=ram.read32(ipc+12);
    check(firstEvent && secondEvent && firstEvent!=secondEvent,"separate handles refer to one application-owned event");
    auto wait=[&](uint32_t handle,uint64_t timeout=0) {
        ram.write32(0x43320,handle);cpu.writeRegister(1,0x43320);cpu.writeRegister(2,1);cpu.writeRegister(3,timeout);
        return p.dispatch(0x18,cpu,bus);
    };
    wait(firstEvent);check(cpu.readRegister(0)==Process::TimedOut,"empty message queue starts unsignaled");
    check(send(focus,root)==0 && ram.read8(ipc+48)==1,"headless profile initially owns virtual focus");
    check(p.setAppletFocus(false) && !p.appletFocused(),"host focus loss changes application state");
    check(send(acquire,root)==0 && ram.read32(ipc+40)==Process::InvalidState && !p.appletForegroundAcquired() && !p.appletFocused(),
        "first acquisition while unfocused fails without stealing focus");
    wait(firstEvent);check(cpu.readRegister(0)==0,"focus notification signals the copied event");
    check(send(focus,root)==0 && ram.read8(ipc+48)==2,"guest reads the actual unfocused state");
    for(unsigned svc:{0x12U,0x17U}) { cpu.writeRegister(0,firstEvent);p.dispatch(uint16_t(svc),cpu,bus);check(cpu.readRegister(0)==Process::InvalidState,"guest cannot reset a service-managed queue event"); }
    cpu.writeRegister(0,firstEvent);p.dispatch(0x11,cpu,bus);check(cpu.readRegister(0)==Process::InvalidHandle,"copied event is not a writable signal endpoint");
    auto receive=focus;put(receive,40,1);
    ram.writeBytes(ipc,receive.data(),receive.size());ram.setPermissions(ipc,4096,Memory::Read);cpu.writeRegister(0,root);p.dispatch(0x21,cpu,bus);
    check(cpu.readRegister(0)==Process::InvalidPointer && snapshot(ram,ipc)==receive,"failed receive reply preserves message");
    ram.setPermissions(ipc,4096,Memory::Read|Memory::Write);
    check(send(receive,root)==0 && ram.read32(ipc+40)==0 && ram.read32(ipc+48)==15,"guest consumes a real FocusStateChanged message");
    wait(secondEvent);check(cpu.readRegister(0)==Process::TimedOut,"last message consumption resets all event copies");
    check(send(receive,root)==0 && ram.read32(ipc+40)==0x680 && ram.read32(ipc+16)==0,"empty ReceiveMessage returns native NoMessages");
    check(wait(secondEvent,UINT64_MAX)==Process::Action::Wait,"wait on empty application event blocks");
    check(p.setAppletFocus(true) && p.resumeWait(cpu) && cpu.readRegister(0)==0,"host focus gain wakes the real kernel wait");
    check(send(receive,root)==0 && ram.read32(ipc+48)==15 && p.appletResourceId()==id,"notification does not replace application identity");
    check(send(acquire,root)==0 && ram.read32(ipc+40)==0 && ram.read32(ipc+4)==12 && ram.read32(ipc+16)==0 &&
        p.appletForegroundAcquired() && p.appletFocused(),"focused acquisition publishes rights with an empty successful reply");
    check(send(acquire,clone)==0 && ram.read32(ipc+40)==0 && p.appletForegroundAcquired(),
        "foreground acquisition is idempotent through a cloned session");
    auto otherAcquire=acquire;put(otherAcquire,20,otherWindow);
    check(send(otherAcquire,other)==0 && ram.read32(ipc+40)==0 && p.appletForegroundAcquired(),
        "independent applet connections share process foreground rights");
    wait(secondEvent);check(cpu.readRegister(0)==Process::TimedOut,"acquisition does not create a focus message");
    check(p.setAppletFocus(false) && p.appletForegroundAcquired(),"focus loss does not revoke already acquired rights");
    check(send(acquire,root)==0 && ram.read32(ipc+40)==0 && !p.appletFocused(),
        "repeated acquisition while unfocused succeeds without changing focus");
    check(send(receive,root)==0 && ram.read32(ipc+48)==15,"focus loss still produces its own notification");
    wait(secondEvent);check(cpu.readRegister(0)==Process::TimedOut,"repeated acquisition adds no duplicate notification");
    check(p.setAppletFocus(true) && send(receive,root)==0 && ram.read32(ipc+48)==15,"restore focused state for bounded queue checks");
    cpu.writeRegister(0,firstEvent);p.dispatch(0x16,cpu,bus);
    bool state=true;
    for(size_t i=0;i<Process::AppletMessageLimit;++i) { state=!state;check(p.setAppletFocus(state),"bounded notification enqueue"); }
    const bool fullState=p.appletFocused();check(!p.setAppletFocus(!state) && p.appletFocused()==fullState,"full queue rejects transition without changing focus");
    check(p.setAppletFocus(state),"unchanged focus creates no duplicate message even with a full queue");
    for(size_t i=0;i<Process::AppletMessageLimit;++i) check(send(receive,root)==0 && ram.read32(ipc+48)==15,"drain every queued focus notification");
    wait(secondEvent);check(cpu.readRegister(0)==Process::TimedOut,"queue signal resets after bounded batch");
    Ipc::Buffer close{};put(close,0,2);send(close,root);send(close,clone);send(close,other);
    const auto [reopened,reopenedWindow]=makeWindow();put(otherIdentity,20,reopenedWindow);
    check(send(otherIdentity,reopened)==0 && ram.read64(ipc+48)==id,"closing applet sessions does not retire the live process resource");
    put(otherAcquire,20,reopenedWindow);
    check(p.appletForegroundAcquired() && send(otherAcquire,reopened)==0 && ram.read32(ipc+40)==0,
        "closing and reopening applet sessions preserves process foreground rights");
    p.checkpoint(cpu,Process::State::Exited);
    check(!p.appletActive() && !p.appletForegroundAcquired() && !p.setAppletFocus(false),"terminal process retires rights and host event production");
    check(send(otherIdentity,reopened)==0 && ram.read32(ipc+40)==Process::InvalidState,"retired resource is no longer published as live");
    check(send(otherAcquire,reopened)==0 && ram.read32(ipc+40)==Process::InvalidState && !p.appletForegroundAcquired(),
        "a terminal process cannot reacquire rights");

    Machine blocked(false,true,true);check(blocked.load(argv[1]),"real SDK wait fixture");const auto blockedId=blocked.process().appletResourceId();
    check(blocked.setAppletFocus(false),"put real SDK application out of focus");
    const auto waiting=blocked.run(100000);
    if(waiting.stop!=Machine::Stop::Waiting) std::cerr<<"Wait path stop="<<int(waiting.stop)<<" code="<<waiting.exitCode<<" error="<<blocked.error()<<'\n';
    check(waiting.stop==Machine::Stop::Waiting && blocked.process().appletActive(),"original libnx waitObjects blocks on the applet message event");
    check(blocked.run(10).instructions==0 && blocked.advanceTime(625) && blocked.run(10).stop==Machine::Stop::Waiting,
        "infinite native wait survives idle virtual time");
    check(blocked.setAppletFocus(true),"host resumes native SDK by focus event");const auto resumed=blocked.run(100000);
    check(resumed.stop==Machine::Stop::Exited && resumed.exitCode==16217 && blocked.process().appletResourceId()==blockedId &&
        !blocked.process().appletActive() && !blocked.process().appletForegroundAcquired(),
        "SDK receives focus, acquires rights, and exits at the remaining SelfController limitation");
    check(!blocked.setAppletFocus(false),"exited machine rejects new applet input");
    Machine reload(false,true,true);check(reload.load(argv[1]),"resource reload fixture");const auto old=reload.process().appletResourceId();
    check(old && old!=blockedId && old!=id,"independent processes get distinct nonzero resource identities");
    auto runToAcquisition=[&](Machine& machine) {
        for(unsigned i=0;i<100000;++i) {
            const auto step=machine.run(1);
            if(machine.process().appletForegroundAcquired()) return true;
            if(step.stop!=Machine::Stop::Budget) return false;
        }
        return false;
    };
    check(runToAcquisition(reload),"acquire rights through the original SDK before reload checks");
    check(reload.setAppletFocus(false),"reload focus state fixture");const auto pc=reload.cpu.getPC();
    const auto missing=std::string(argv[1])+".missing";
    check(!reload.load(missing.c_str()) && reload.cpu.getPC()==pc && reload.process().appletResourceId()==old &&
        !reload.process().appletFocused() && reload.process().appletForegroundAcquired(),
        "failed load preserves identity, focus and acquired rights");
    check(reload.load(argv[1]) && reload.process().appletResourceId()!=old && reload.process().appletFocused() &&
        !reload.process().appletForegroundAcquired(),"successful reload starts a fresh application resource without rights");
    check(runToAcquisition(reload),"fresh process acquires its own foreground rights");
    reload.cpu.setPC(Machine::RAM_SIZE);
    check(reload.run(1).stop==Machine::Stop::Error && !reload.process().appletActive() && !reload.process().appletForegroundAcquired(),
        "a CPU fault retires foreground rights");
    std::cout<<"Applet identity="<<id<<" SDK wait instructions="<<waiting.instructions<<" resume="<<resumed.instructions<<" failures="<<failures<<'\n';
    return failures?1:0;
}
