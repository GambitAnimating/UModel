// Simple UI library.
// Copyright (C) 2018 Konstantin Nosov
// Licensed under the BSD license. See LICENSE.txt file in the project root for full license information.

#include "BaseDialog.h"

#if HAS_UI

#include "UIPrivate.h"

#define DEBUG_LAYOUT				0

#if DEBUG_LAYOUT
#define DBG_LAYOUT(...)				appPrintf(__VA_ARGS__)
#else
#define DBG_LAYOUT(...)
#endif


#if DEBUG_LAYOUT

static int DebugLayoutDepth = 0;

static const char* GetDebugLayoutIndent()
{
#define MAX_INDENT 32
	static char indent[MAX_INDENT*2+1];
	if (!indent[0]) memset(indent, ' ', sizeof(indent)-1);
	return &indent[MAX_INDENT*2 - DebugLayoutDepth*2];
}

const char* GetDebugLabel(const UIElement* ctl)
{
#define P(type, field)		\
	if (ctl->IsA(#type)) return *static_cast<const type*>(ctl)->field;

	P(UIButton, Label)
	P(UIMenuButton, Label)
	P(UICheckbox, Label)
	P(UIRadioButton, Label)
	P(UILabel, Label)

	return "";
#undef P
}

#endif // DEBUG_LAYOUT


/*-----------------------------------------------------------------------------
	UILayoutHelper
-----------------------------------------------------------------------------*/

UILayoutHelper::UILayoutHelper(UIGroup* InGroup, int InFlags)
: Flags(InFlags)
, ParentRect(InGroup->Rect)
{
	// Fix for wrong height. TODO: should reimplement layout code to work without it!
	ParentRect.Height = InGroup->Layout.Height;

	if (!(Flags & GROUP_NO_BORDER))
	{
		CursorX = GROUP_INDENT;
		CursorY = GROUP_MARGIN_TOP;
	}
	else
	{
		CursorX = CursorY = 0;
	}
	DBG_LAYOUT("%sNewLayout (%s): x=%d y=%d w=%d h=%d\n", GetDebugLayoutIndent(), GetDebugLabel(InGroup),
		ParentRect.X, ParentRect.Y, ParentRect.Width, ParentRect.Height);
}

void UILayoutHelper::AllocateSpace(const UIRect& src, UIRect& dst)
{
	guard(UILayoutHelper::AllocateSpace);

	int baseX = ParentRect.X + CursorX;
	int baseY = ParentRect.Y + CursorY;
	int parentWidth = ParentRect.Width;
	int rightMargin = ParentRect.X + ParentRect.Width;

	if (!(Flags & GROUP_NO_BORDER))
	{
		parentWidth -= GROUP_INDENT * 2;
		rightMargin -= GROUP_INDENT;
	}

	int x = src.X;
	int y = src.Y;
	int w = src.Width;
	int h = src.Height;

	DBG_LAYOUT("%s... AllocSpace (%d %d %d %d) IN: Curs: %d,%d W: %d -- ", GetDebugLayoutIndent(), x, y, w, h, CursorX, CursorY, parentWidth);

	// Compute width
	if (w < 0)
	{
		if (w == -1 && (Flags & GROUP_HORIZONTAL_LAYOUT))
			w = AutoWidth;
		else
			w = int(UIElement::DecodeWidth(w) * parentWidth);
	}

	// Compute height
	if (h < 0 && ParentRect.Height > 0)
	{
		h = int(UIElement::DecodeWidth(h) * ParentRect.Height);
	}
	assert(h >= 0);

	// Compute X
	if (x == -1)
	{
		// Automatic X
		x = baseX;
		//!! UseHorizontalLayout()
		if ((Flags & (GROUP_NO_AUTO_LAYOUT|GROUP_HORIZONTAL_LAYOUT)) == GROUP_HORIZONTAL_LAYOUT)
			CursorX += w;
	}
	else if (x < 0)
	{
		// X is fractional value
		x = baseX + int(UIElement::DecodeWidth(x) * parentWidth);	// left border of parent control, 'x' is relative value
	}
	else
	{
		// X is absolute value
		x = baseX + x;					// treat 'x' as relative value
	}

	// Clamp width
	if (x + w > rightMargin)
		w = rightMargin - x;

	// Compute Y
	if (y == -1)
	{
		// Automatic Y
		y = baseY;
		//!! UseVerticalLayout()
		if ((Flags & (GROUP_NO_AUTO_LAYOUT|GROUP_HORIZONTAL_LAYOUT)) == 0)
			CursorY += h;
	}
	else
	{
		// Y is absolute value, wo don't support fractional values yer
		y = baseY + y;					// treat 'y' as relative value
		// don't change 'Height'
	}

//	h = unchanged; (i.e. do not clamp height)

	DBG_LAYOUT("OUT: (%d %d %d %d) Curs: %d,%d\n", x, y, w, h, CursorX, CursorY);

	dst.X = x;
	dst.Y = y;
	dst.Width = w;
	dst.Height = h;

	unguard;
}

void UILayoutHelper::AddVerticalSpace(int size)
{
	if ((Flags & (GROUP_NO_AUTO_LAYOUT|GROUP_HORIZONTAL_LAYOUT)) == 0)
		CursorY += (size >= 0 ? size : VERTICAL_SPACING);
}

void UILayoutHelper::AddHorizontalSpace(int size)
{
	if ((Flags & (GROUP_NO_AUTO_LAYOUT|GROUP_HORIZONTAL_LAYOUT)) == GROUP_HORIZONTAL_LAYOUT)
		CursorX += (size >= 0 ? size : HORIZONTAL_SPACING);
}


/*-----------------------------------------------------------------------------
	UIGroup layout
-----------------------------------------------------------------------------*/

void UIGroup::UpdateLayout(UILayoutHelper* parentLayout)
{
	guard(UIGroup::UpdateLayout);

	if (parentLayout)
	{
//		if (!(Flags & GROUP_NO_BORDER)) -- makes control layout looking awful
		parentLayout->AddVertSpace();
		// request x, y and width; height is not available yet
		int saveHeight = Layout.Height;
		Layout.Height = 0;
		// Add control with zero height. This will compute all values
		// of group's position, but won't reserve vertical space.
		parentLayout->AddControl(this);
		Layout.Height = saveHeight;
	}

	UILayoutHelper layout(this, Flags);
	if (!parentLayout)
	{
		// this is a dialog, add some space on top for better layout
		layout.AddVertSpace();
	}

#if DEBUG_LAYOUT
	DBG_LAYOUT("%sgroup \"%s\" cursor: %d %d\n", GetDebugLayoutIndent(), *Label, layout.CursorX, layout.CursorY);
	DebugLayoutDepth++;
#endif

	// determine default width of control in horizontal layout; this value will be used for
	// all controls which width was not specified (for Width==-1)
	layout.AutoWidth = 0;
	int horizontalSpacing = 0;
	if (layout.UseHorizontalLayout())
	{
		int totalWidth = 0;					// total width of controls with specified width
		int numAutoWidthControls = 0;		// number of controls with width set to -1
		int numControls = 0;
		int parentWidth = Rect.Width;		// width of space for children controls
		if (!(Flags & GROUP_NO_BORDER))
			parentWidth -= GROUP_INDENT * 2;

		for (UIElement* control = FirstChild; control; control = control->NextChild)
		{
			numControls++;
			// get width of control
			int w = control->Layout.Width;
			if (w == -1)
			{
				numAutoWidthControls++;
			}
			else if (w < 0)
			{
				w = int(DecodeWidth(w) * parentWidth);
				totalWidth += w;
			}
			else
			{
				totalWidth += w;
			}
		}
		if (totalWidth > parentWidth)
			appNotify("Group(%s) is not wide enough to store children controls: %d < %d", *Label, parentWidth, totalWidth);
		if (numAutoWidthControls)
			layout.AutoWidth = (parentWidth - totalWidth) / numAutoWidthControls;
		if (Flags & GROUP_HORIZONTAL_SPACING)
		{
			if (numAutoWidthControls)
			{
				appNotify("Group(%s) has GROUP_HORIZONTAL_SPACING and auto-width controls", *Label);
			}
			else
			{
				if (numControls > 1)
					horizontalSpacing = (parentWidth - totalWidth) / (numControls - 1);
			}
		}
	}

	int maxControlY = Rect.Y + Rect.Height;

	for (UIElement* control = FirstChild; control; control = control->NextChild)
	{
		// evenly space controls for horizontal layout, when requested
		if (horizontalSpacing > 0 && control != FirstChild)
			layout.AddHorzSpace(horizontalSpacing);

		DBG_LAYOUT("%s%s \"%s\": x=%d y=%d w=%d h=%d\n", GetDebugLayoutIndent(),
			control->ClassName(), GetDebugLabel(control), control->Layout.X, control->Layout.Y, control->Layout.Width, control->Layout.Height);

		control->UpdateLayout(&layout);

		int bottom = control->Rect.Y + control->Rect.Height;
		if (bottom > maxControlY)
			maxControlY = bottom;
	}

	Rect.Height = max(Rect.Height, maxControlY - Rect.Y);

	if (!(Flags & GROUP_NO_BORDER))
		Rect.Height += GROUP_MARGIN_BOTTOM;

	if (parentLayout && parentLayout->UseVerticalLayout())
	{
		// We've reserved no vertical space before (when called parentLayout->AddControl()), so
		// we should fix that now.
		parentLayout->AddVertSpace(Rect.Height);
	}
#if DEBUG_LAYOUT
	DebugLayoutDepth--;
#endif

	if (parentLayout)
		parentLayout->AddVertSpace();

	unguard;
}

int UIElement::ComputeWidth() const
{
	return Layout.Width;
}

int UIElement::ComputeHeight() const
{
	return Layout.Height;
}

struct UILayoutHelper2
{
	int			TotalSize;			// total size in pixels (dialog units) of controls with known sizes
	float		TotalFrac;			// total fraction size of controls with proportional sizes
};

void UIGroup::ComputeLayout()
{
	guard(UIGroup::ComputeLayout);

///	const UIRect* parentRect = Parent ? &Parent->Rect : NULL;
	int groupBorderTop = 0;
	int groupBorderLeft = 0;
	int groupBorderWidth = 0;
	int groupBorderHeight = 0;
	if (!(Flags & GROUP_NO_BORDER))
	{
		groupBorderTop = GROUP_MARGIN_TOP;
		groupBorderLeft = GROUP_INDENT;
		groupBorderWidth = GROUP_INDENT * 2;
		groupBorderHeight = GROUP_MARGIN_TOP + GROUP_MARGIN_BOTTOM;
	}

	if (Flags & GROUP_HORIZONTAL_LAYOUT)
	{
		// Automatic horizontal layout
		if (Rect.Width <= 0)
		{
		}
		else
		{
		}
	}
	else if (!(Flags & GROUP_NO_AUTO_LAYOUT))
	{
		// Common layout code
		int TotalSize = 0;
		int MarginsSize = 0;
		float TotalFrac = 0.0f;
		int MaxWidth = 0;
		for (UIElement* child = FirstChild; child; child = child->NextChild)
		{
			child->Rect = child->Layout;

			int h = child->Rect.Height;
			int w = child->Rect.Width;

			if (child->IsGroup && (Rect.Height <= 0 || Rect.Width <= 0) && (child->Rect.Height < 0 || child->Rect.Width < 0))
			{
				// We should compute size of the child group
				static_cast<UIGroup*>(child)->ComputeLayout();
				w = child->Rect.Width;
				h = child->Rect.Height;
			}

			if (h >= 0)
			{
				TotalSize += h;
			}
			else
			{
				TotalFrac += DecodeWidth(h);
			}

			if (w < 0)
			{
				w = child->MinWidth;
			}

			MarginsSize += child->TopMargin + child->BottomMargin;
			MaxWidth = max(w, MaxWidth);
		}

		if (Rect.Width <= 0)
		{
			Rect.Width = MaxWidth + groupBorderWidth;
		}

		if (Rect.Height <= 0)
		{
			// Automatic vertical layout
			float FracScale = 0;
			for (UIElement* child = FirstChild; child; child = child->NextChild)
			{
				int h = child->Rect.Height;
				if (h < 0)
				{
					float frac = DecodeWidth(h);
					int minHeight = child->IsGroup ? child->Rect.Height : child->MinHeight;
					float localFracScale = minHeight / frac;
					if (localFracScale > FracScale)
					{
						FracScale = localFracScale;
					}
				}
			}

			Rect.Height = TotalSize + MarginsSize + FracScale + groupBorderHeight;
		}
		else
		{
			// Size is known, perform control layout
			int groupWidth = Rect.Width - groupBorderWidth;
			int groupHeight = Rect.Height - groupBorderHeight - MarginsSize;
			int SizeOfComputedControls = groupHeight - TotalSize;
			float FracScale = (TotalFrac > 0) ? SizeOfComputedControls / TotalFrac : 0;

			// Place controls inside the group
			int x = Rect.X + groupBorderLeft;
			int y = Rect.Y + groupBorderTop;
			for (UIElement* child = FirstChild; child; child = child->NextChild)
			{
				y += child->TopMargin;

				int h = child->Rect.Height;
				int w = child->Rect.Width;

				if (h < 0)
				{
					h = DecodeWidth(h) * FracScale;
				}

				if (w < 0)
				{
					w = DecodeWidth(w) * groupWidth;
				}

				child->Rect.X = x;
				child->Rect.Y = y;
				child->Rect.Width = w;
				child->Rect.Height = h;

				// Perform layout for child group
				if (child->IsGroup)
				{
					static_cast<UIGroup*>(child)->ComputeLayout();
				}

				y += h + child->BottomMargin;
			}
		}
	}

	unguard;
}

#endif // HAS_UI
