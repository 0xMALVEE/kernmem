/*
 * main.cpp - Apex Legends ESP entry point.
 *
 * This is the whole of a game's entry point: construct the provider, hand it to
 * the shared framework, done. Everything else - window, drawing, attach retry,
 * frame loop, driver handshake - is in sdk/client.
 */
#include "provider.h"

#include "../../sdk/client/esp.h"

int main(int, char**)
{
    apex::Provider provider;
    esp::Run(provider);

    return 0;
}
