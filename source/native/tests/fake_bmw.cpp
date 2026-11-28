#include "fake_bmw.h"
#include <map>
#include <stdlib.h>
#include <string.h>

std::vector<FakeUpdate> fake_updates;
std::vector<FakePost> fake_posts;

namespace {
typedef bmw::Delegate<bool, const bmw::Parcel&> Handler;
std::map<unsigned long long, Handler> handlers;

struct ParcelData { std::vector<unsigned> words; unsigned pos; ParcelData() : pos(0) {} };
struct MessageData { unsigned id; std::vector<unsigned> words; };

ParcelData*& parcel_data(const bmw::Parcel& p) {
    return *reinterpret_cast<ParcelData**>(const_cast<unsigned*>(p._raw));
}
MessageData*& message_data(const bmw::Message& m) {
    return *reinterpret_cast<MessageData**>(const_cast<unsigned char*>(m.opaque_.storage));
}
void record(unsigned model, char kind, unsigned number, const char* text) {
    FakeUpdate u;
    u.model = model; u.kind = kind; u.number = number; u.text = text ? text : "";
    fake_updates.push_back(u);
}
}  // namespace

void fake_reset() {
    fake_updates.clear();
    fake_posts.clear();
    handlers.clear();
}

bool fake_fire(unsigned state, unsigned action, const bmw::Parcel& event) {
    std::map<unsigned long long, Handler>::iterator it = handlers.find((unsigned long long)state << 32 | action);
    return it != handlers.end() && it->second.m_exec(it->second.m_obj, event);
}

namespace bmw {

template <> BasicString<char>::BasicString(const char* s) : m_data(strdup(s)) {}
template <> BasicString<char>::BasicString(const BasicString& o) : m_data(strdup(o.m_data)) {}
template <> BasicString<char>::~BasicString() { free(m_data); }
template <> BasicString<char>& BasicString<char>::operator=(const BasicString& o) {
    if (this != &o) { free(m_data); m_data = strdup(o.m_data); }
    return *this;
}

ImageID::ImageID(unsigned int i) : id(i), type(0) {}
TextID::TextID(unsigned int id) : m_a(id), m_b(0) {}

Parcel::Parcel() { memset(_raw, 0, sizeof _raw); parcel_data(*this) = new ParcelData; }
Parcel::Parcel(bool value) { memset(_raw, 0, sizeof _raw); parcel_data(*this) = new ParcelData; writeUInt32(value ? 1u : 0u); }
Parcel::~Parcel() { delete parcel_data(*this); }
bool Parcel::readUInt32(unsigned int& v) const {
    ParcelData* d = parcel_data(*this);
    if (d->pos >= d->words.size()) return false;
    v = d->words[d->pos++];
    return true;
}
bool Parcel::readInt32(int& v) const { unsigned u; if (!readUInt32(u)) return false; v = (int)u; return true; }
bool Parcel::readBool(bool& v) const { unsigned u; if (!readUInt32(u)) return false; v = u != 0; return true; }
void Parcel::writeUInt32(unsigned int v) { parcel_data(*this)->words.push_back(v); }

Message::Message(unsigned int id, const Parcel& data) {
    MessageData* m = new MessageData;
    m->id = id;
    m->words = parcel_data(data)->words;
    message_data(*this) = m;
}
Message::Message(const Message& o) { message_data(*this) = new MessageData(*message_data(o)); }
Message::~Message() { delete message_data(*this); }

PluginApp::PluginApp(IDispatcher&, BasicString<char>, unsigned int, unsigned int, RHMIInterfaceVersion,
                     const Bundle&, const Bundle&) {}
PluginApp::~PluginApp() {}

void PluginApp::updateModel(unsigned int, const DataTableBase&) {}
void PluginApp::updateModel(unsigned int model, const BasicString<char>& s) { record(model, 's', 0, s.m_data); }
void PluginApp::updateModel(unsigned int model, int value) { record(model, 'n', (unsigned)value, 0); }
void PluginApp::updateModel(unsigned int model, const ImageID& image) { record(model, 'i', image.id, 0); }
void PluginApp::updateModel(unsigned int, const TextID&) {}
#ifndef FAKE_BMW_NO_BOOL_MODEL
// The separate full-screen public SDK omits this unused overload.
void PluginApp::updateModel(unsigned int, bool) {}
#endif
void PluginApp::addHandler(unsigned int action, const Delegate<bool, const Parcel&>& d) { handlers[action] = d; }
void PluginApp::addHandler(unsigned int state, unsigned int action, const Delegate<bool, const Parcel&>& d) {
    handlers[(unsigned long long)state << 32 | action] = d;
}
void PluginApp::addHandlerWithMap(unsigned int, unsigned int, const Delegate<bool, const Map<unsigned int, Variant>&>&) {}
void PluginApp::setProperty(unsigned int, unsigned int, const Parcel&) {}
void PluginApp::triggerEvent(unsigned int) {}
void PluginApp::triggerEvent(unsigned int, const Parcel&) {}

bool PluginApp::start() { return true; }
bool PluginApp::stop() { return true; }
bool PluginApp::activate() { return true; }
bool PluginApp::deactivate() { return true; }
bool PluginApp::init() { return true; }
bool PluginApp::finalize() { return true; }
void PluginApp::post(const Message& m) { post(m, 0); }
void PluginApp::post(const Message& m, unsigned int delay) {
    FakePost p;
    p.id = message_data(m)->id;
    p.words = message_data(m)->words;
    p.delay = delay;
    fake_posts.push_back(p);
}
void PluginApp::post(Runnable*) {}
void PluginApp::post(Runnable*, unsigned int) {}
bool PluginApp::getName() const { return true; }
bool PluginApp::getResourceFile(ResourceType::ResourceTypes) const { return true; }
bool PluginApp::getVersion(unsigned int&, unsigned int&, unsigned int&) const { return true; }
bool PluginApp::getRhmiVersion(RHMIInterfaceVersion&) const { return true; }
bool PluginApp::invokeCommand(unsigned int, const Parcel&) { return true; }
bool PluginApp::updateModel(unsigned int, const Parcel&, unsigned char) { return true; }
bool PluginApp::handleEvent(unsigned int, unsigned int, const Parcel&) { return true; }
bool PluginApp::invokeDataUpdate(unsigned int, const Parcel&) { return false; }
bool PluginApp::requestDataValue(unsigned int) { return true; }
bool PluginApp::onError(unsigned int, const BasicString<char>&) { return true; }
bool PluginApp::onInit() { return true; }
bool PluginApp::onFinalize() { return true; }
bool PluginApp::getCapabilities(const std::string&) { return true; }
bool PluginApp::onCheckAppAvailable() { return true; }
bool PluginApp::handleMessage(const Message&) { return true; }
bool PluginApp::invokeCommandWithMap(unsigned int, const Map<unsigned int, Variant>&) { return true; }
bool PluginApp::updateModelWithMap(unsigned int, const Map<unsigned int, Variant>&) { return true; }
bool PluginApp::handleEventWithMap(unsigned int, unsigned int, const Map<unsigned int, Variant>&) { return true; }
bool PluginApp::invokeCDSUpdate(BasicString<char>&, BasicString<unsigned short>&) { return true; }
bool PluginApp::invokeVoiceStateChanged(unsigned short) { return true; }
bool PluginApp::invokeAppManagerUpdate(const BasicString<char>&) { return true; }
bool PluginApp::invokeSdsEvent(unsigned int, const Map<unsigned int, Variant>&) { return true; }

}  // namespace bmw
