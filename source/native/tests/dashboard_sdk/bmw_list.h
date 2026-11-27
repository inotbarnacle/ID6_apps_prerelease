#ifndef TEST_DASHBOARD_BMW_H
#define TEST_DASHBOARD_BMW_H
#include <map>
#include <string>
#include <vector>
#include <stdint.h>
#include <assert.h>
namespace bmw {
class IDispatcher {};
class Bundle {};
struct RHMIInterfaceVersion { unsigned value; };
template<class T> class BasicString {
public: std::basic_string<T> value; explicit BasicString(const T* s):value(s){}
};
struct ImageID { unsigned id; explicit ImageID(unsigned i):id(i){} };
class Variant {
public:
    class IObjectContainer { public: virtual ~IObjectContainer(){}; };
    IObjectContainer* m_impl;
    Variant():m_impl(0){}
};
class StateBoolContainer : public Variant::IObjectContainer {
public: unsigned char value; explicit StateBoolContainer(unsigned char n):value(n){}
};
template<class K,class V> class Map {
public:
    std::map<K,V> values;
    const V* find(unsigned key)const {
        typename std::map<K,V>::const_iterator i=values.find(key);
        return i==values.end() ? 0 : &i->second;
    }
};
class Parcel {
    mutable unsigned offset_;
public:
    std::vector<unsigned> values;
    Parcel():offset_(0){}
    explicit Parcel(bool value):offset_(0){values.push_back(value);}
    bool readUInt32(unsigned& value)const {
        if(offset_>=values.size())return false;
        value=values[offset_++];return true;
    }
    bool readBool(bool& value)const {unsigned n;if(!readUInt32(n))return false;value=n!=0;return true;}
    void writeUInt32(unsigned value){values.push_back(value);}
};
class Message {
public: unsigned id; Parcel data;
    Message(unsigned i,const Parcel& p):id(i),data(p){}
};
template<class R,class A> class Delegate {
public:
    void* m_obj; R(*m_exec)(void*,A);
    template<class C,R(C::*Method)(A)> static R execImpl(void* self,A arg){return (static_cast<C*>(self)->*Method)(arg);}
};
class PluginApp {
    typedef Delegate<bool,const Parcel&> Action;
    typedef Delegate<bool,const Map<unsigned,Variant>&> StateAction;
    std::map<unsigned,Action> handlers_;
    std::map<unsigned,Action> actions_;
    std::map<unsigned,StateAction> state_handlers_;
public:
    struct Posted {
        unsigned id,delay;
        std::vector<unsigned> values;
        Posted(const Message& message,unsigned ms):id(message.id),delay(ms),values(message.data.values){}
    };
    unsigned posts;
    std::vector<unsigned> queued;
    std::vector<Posted> messages;
    std::map<unsigned,std::string> texts;
    std::map<unsigned,std::wstring> wide_texts;
    std::map<unsigned,unsigned> images;
    std::map<unsigned,bool> bools;
    std::map<std::pair<unsigned,unsigned>,unsigned> properties;
    PluginApp(IDispatcher&,BasicString<char>,unsigned,unsigned,RHMIInterfaceVersion,const Bundle&,const Bundle&):posts(0){}
    virtual ~PluginApp(){}
    virtual bool onInit(){return true;}
    virtual bool onFinalize(){return true;}
    virtual bool invokeDataUpdate(unsigned,const Parcel&){return false;}
    void updateModel(unsigned model,const BasicString<char>& value){texts[model]=value.value;}
    void updateModel(unsigned model,const wchar_t* value){wide_texts[model]=value;}
    void updateModel(unsigned model,const ImageID& value){images[model]=value.id;}
    void updateModel(unsigned model,bool value){bools[model]=value;}
    void setProperty(unsigned component,unsigned property,const Parcel& value){
        assert(!value.values.empty());properties[std::make_pair(component,property)]=value.values[0];
    }
    void addHandler(unsigned action,const Action& handler){actions_[action]=handler;}
    void addHandler(unsigned state,unsigned event,const Action& action){handlers_[state*100u+event]=action;}
    void addHandlerWithMap(unsigned state,unsigned event,const StateAction& action){state_handlers_[state*100u+event]=action;}
    void post(const Message& message,unsigned delay){posts++;queued=message.data.values;messages.push_back(Posted(message,delay));}
    bool testMap(unsigned state,unsigned event,const Map<unsigned,Variant>& data){
        std::map<unsigned,StateAction>::iterator i=state_handlers_.find(state*100u+event);
        return i!=state_handlers_.end() && i->second.m_exec(i->second.m_obj,data);
    }
    bool testState(unsigned state,unsigned event,unsigned key,unsigned char value){
        StateBoolContainer container(value);Variant v;v.m_impl=&container;
        Map<unsigned,Variant> data;data.values[key]=v;return testMap(state,event,data);
    }
    void testEvent(unsigned state,bool show){
        if(state_handlers_.count(state*100u+11u)){
            if(show){testState(state,11u,23u,1);testState(state,1u,4u,1);}
            else {testState(state,1u,4u,0);testState(state,11u,23u,0);}
        } else {Parcel p(show);Action a=handlers_.at(state*100u+11u);a.m_exec(a.m_obj,p);}
    }
    void testTick(){Parcel p;p.writeUInt32(queued.at(1));invokeDataUpdate(queued.at(0),p);}
    void testAction(unsigned action){Parcel p;Action a=actions_.at(action);a.m_exec(a.m_obj,p);}
    unsigned testLastMessage(unsigned key)const {
        for(unsigned i=static_cast<unsigned>(messages.size());i>0;--i)
            if(!messages[i-1].values.empty() && messages[i-1].values[0]==key)return i-1;
        return static_cast<unsigned>(messages.size());
    }
    void testDeliver(unsigned index){
        const Posted message=messages.at(index);
        assert(message.id==0x40000003u && !message.values.empty());
        Parcel data;for(unsigned i=1;i<message.values.size();++i)data.writeUInt32(message.values[i]);
        invokeDataUpdate(message.values[0],data);
    }
};
}
#endif
