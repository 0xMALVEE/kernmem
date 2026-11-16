/*
 * main.cpp - League of Legends ESP entry point.
 *
 * Three lines: construct the provider, hand it to the shared framework, done.
 * The overlay, drawing, attach retry and frame loop live in sdk/client.
 */
#include "provider.h"

#include "../../sdk/client/esp.h"

int main(int, char**)
{
    league::Provider provider;
    esp::Run(provider);

    return 0;
}
