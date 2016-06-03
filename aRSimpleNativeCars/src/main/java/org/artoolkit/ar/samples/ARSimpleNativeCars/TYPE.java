package org.artoolkit.ar.samples.ARSimpleNativeCars;

/**
 * Created by adaradici on 6/1/2016.
 */
public enum TYPE {
    BEAR("bear"), DEER("deer");

    private String name;

    TYPE(String name) {
        this.name = name;
    }

    public String toString() {
        return name;
    }
}
