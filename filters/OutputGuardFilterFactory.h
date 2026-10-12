/*
    This file is part of Equalizer APO, a system-wide equalizer.
    Copyright (C) 2026  Equalizer APO contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#pragma once

#include "IFilterFactory.h"

class OutputGuardFilterFactory : public IFilterFactory
{
public:
	std::vector<IFilter*> createFilter(const std::wstring& configPath, std::wstring& command, std::wstring& parameters) override;
};
