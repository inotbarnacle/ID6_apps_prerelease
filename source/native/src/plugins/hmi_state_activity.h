#ifndef ID6_HMI_STATE_ACTIVITY_H
#define ID6_HMI_STATE_ACTIVITY_H

/* State events use the OEM Map contract: 11/key23 visibility, 1/key4 focus.
 * ActiveSeating reads its bool container immediately after the vptr. Keep that
 * ABI (4 bytes on the target), and reject missing or non-boolean byte values. */
inline bool hmiStateBool(const bmw::Map<unsigned,bmw::Variant>& event,unsigned key,bool& value) {
    const bmw::Variant* item=event.find(key);
    if (!item || !item->m_impl) return false;
    const unsigned byte=reinterpret_cast<const unsigned char*>(item->m_impl)[sizeof(void*)];
    if (byte>1u) return false;
    value=byte!=0;
    return true;
}

inline bool hmiStateActivity(unsigned& shown,unsigned& focused,unsigned bit,unsigned key,
                             const bmw::Map<unsigned,bmw::Variant>& event) {
    bool value=false;
    hmiStateBool(event,key,value); // Missing/malformed activity stops collection until a valid event.
    if (key==23u) {
        if (value) shown|=bit;
        else { shown&=~bit;focused&=~bit; }
    } else {
        if (value) focused|=bit;
        else focused&=~bit;
    }
    return (shown&focused&bit)!=0;
}

#endif
