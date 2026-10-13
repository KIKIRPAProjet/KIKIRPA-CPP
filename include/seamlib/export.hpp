#pragma once


// Gestion export/import Windows
//
// Si on compile une DLL :
//      KIRKSEAMLINES_BUILD
//          -> export
//
// Si on utilise une DLL :
//          -> import
//
// Pour une librairie statique :
//          -> aucune macro nécessaire


#ifdef _WIN32

#ifdef KIRKSEAMLINES_SHARED

#ifdef KIRKSEAMLINES_BUILD

#define KIRK_API __declspec(dllexport)

#else

#define KIRK_API __declspec(dllimport)

#endif

#else

#define KIRK_API

#endif


#else

#define KIRK_API

#endif