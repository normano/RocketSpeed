// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from rocketspeed.djinni

package org.rocketspeed;

import java.util.ArrayList;

public final class ConfigurationImpl {


    /*package*/ final ArrayList<HostId> pilots;

    /*package*/ final ArrayList<HostId> copilots;

    /*package*/ final int tenantId;

    public ConfigurationImpl(
            ArrayList<HostId> pilots,
            ArrayList<HostId> copilots,
            int tenantId) {
        this.pilots = pilots;
        this.copilots = copilots;
        this.tenantId = tenantId;
    }

    public ArrayList<HostId> getPilots() {
        return pilots;
    }

    public ArrayList<HostId> getCopilots() {
        return copilots;
    }

    public int getTenantId() {
        return tenantId;
    }
}