/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2020 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 6 End-User License
   Agreement and JUCE Privacy Policy (both effective as of the 16th June 2020).

   End User License Agreement: www.juce.com/juce-6-licence
   Privacy Policy: www.juce.com/juce-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

Image juce_createIconForFile (const File& file);


//==============================================================================
FileListComponent::FileListComponent (DirectoryContentsList& listToShow)
    : ListBox ({}, nullptr),
      DirectoryContentsDisplayComponent (listToShow),
      lastDirectory (listToShow.getDirectory())
{
    setTitle ("Files");
    setModel (this);
    directoryContentsList.addChangeListener (this);
}

FileListComponent::~FileListComponent()
{
    directoryContentsList.removeChangeListener (this);
}

int FileListComponent::getNumSelectedFiles() const
{
    return getNumSelectedRows();
}

File FileListComponent::getSelectedFile (int index) const
{
    return directoryContentsList.getFile (getSelectedRow (index));
}

void FileListComponent::deselectAllFiles()
{
    deselectAllRows();
}

void FileListComponent::scrollToTop()
{
    getVerticalScrollBar().setCurrentRangeStart (0);
}

void FileListComponent::setSelectedFile (const File& f)
{
    //DBG(getTitle() + "::FLC::setSelectedFile " + f.getFullPathName() + " : " + directoryContentsList.getDirectory().getFullPathName());
    for (int i = directoryContentsList.getNumFiles(); --i >= 0;)
    {
        if (directoryContentsList.getFile (i) == f)
        {
            //DBG(getTitle() + "::FLC:: selecting " + f.getFullPathName() + " at index " + juce::String(i) + ".Clear waiting. ###");
			fileWaitingToBeSelected = File();

            selectRow (i);
            currentSelectedFile = getSelectedFile();
            return;
        }
    }

    //DBG(getTitle() + "::FLC::setSelectedFile. Waiting to select " + f.getFullPathName());

    deselectAllRows();
    fileWaitingToBeSelected = f;
}

void FileListComponent::setContentDirectory(const File& f, bool includeDirectories, bool includeFiles)
{
    //DBG(getTitle() + "::FLC::setContentDirectory " + f.getFullPathName());
    currentSelectedFile = getSelectedFile();
    lastDirectory = directoryContentsList.getDirectory();
    directoryContentsList.setDirectory(f, includeDirectories, includeFiles);
}
//==============================================================================
void FileListComponent::changeListenerCallback (ChangeBroadcaster*)
{
    //DBG(getTitle() + "::FLC:: change listener callback");
	
    updateContent();

	// Added check to see if the file waiting to be selected is a child of the current directory.

    if (lastDirectory != directoryContentsList.getDirectory())
    {
        //DBG(getTitle() + "::FLC: last dir is not DCL dir. Last " + lastDirectory.getFullPathName() + ". DCL : " + directoryContentsList.getDirectory().getFullPathName());
        lastDirectory = directoryContentsList.getDirectory();
    }

	if (!fileWaitingToBeSelected.getFileName().startsWith("VOLUMES:") && fileWaitingToBeSelected.isAChildOf(directoryContentsList.getDirectory()))
    {
        //DBG(getTitle() + "::FLC:: Waiting file is a child of DCL dir " + fileWaitingToBeSelected.getFullPathName());
        setSelectedFile(fileWaitingToBeSelected);
    }
    else if(currentSelectedFile.isAChildOf(directoryContentsList.getDirectory()))
    {
        //DBG(getTitle() + "::FLC:: Current selected file is a child of DCL dir " + fileWaitingToBeSelected.getFullPathName());
        setSelectedFile(currentSelectedFile);
    }
	else 
    {
        //DBG(getTitle() + "::FLC:: DCL loaded. Clear waiting.");
        fileWaitingToBeSelected = File();
        deselectAllRows();
    }
}

//==============================================================================
class FileListComponent::ItemComponent  : public Component,
                                          private TimeSliceClient,
                                          private AsyncUpdater
{
public:
    ItemComponent (FileListComponent& fc, TimeSliceThread& t)
        : owner (fc), thread (t)
    {
    }

    ~ItemComponent() override
    {
        thread.removeTimeSliceClient (this);
    }

    //==============================================================================
    void paint (Graphics& g) override
    {
        getLookAndFeel().drawFileBrowserRow (g, getWidth(), getHeight(),
                                             file, file.getFileName(),
                                             &icon, fileSize, modTime,
                                             isDirectory, highlighted,
                                             index, owner);
    }

    void mouseDown (const MouseEvent& e) override
    {
        owner.selectRowsBasedOnModifierKeys (index, e.mods, true);
        owner.sendMouseClickMessage (file, e);
    }

    void mouseDoubleClick (const MouseEvent&) override
    {
        owner.sendDoubleClickMessage (file);
    }

    void update (const File& root, const DirectoryContentsList::FileInfo* fileInfo,
                 int newIndex, bool nowHighlighted)
    {
        thread.removeTimeSliceClient (this);

        if (nowHighlighted != highlighted || newIndex != index)
        {
            index = newIndex;
            highlighted = nowHighlighted;
            repaint();
        }

        File newFile;
        String newFileSize, newModTime;

        if (fileInfo != nullptr)
        {
            newFile = root.getChildFile (fileInfo->filename);
            newFileSize = File::descriptionOfSizeInBytes (fileInfo->fileSize);
            newModTime = fileInfo->modificationTime.formatted ("%d %b '%y %H:%M");
        }

        if (newFile != file
             || fileSize != newFileSize
             || modTime != newModTime)
        {
            file = newFile;
            fileSize = newFileSize;
            modTime = newModTime;
            icon = Image();
            isDirectory = fileInfo != nullptr && fileInfo->isDirectory;

            repaint();
        }

        if (file != File() && icon.isNull() && ! isDirectory)
        {
            updateIcon (true);

            if (! icon.isValid())
                thread.addTimeSliceClient (this);
        }
    }

    int useTimeSlice() override
    {
        updateIcon (false);
        return -1;
    }

    void handleAsyncUpdate() override
    {
        repaint();
    }

private:
    //==============================================================================
    FileListComponent& owner;
    TimeSliceThread& thread;
    File file;
    String fileSize, modTime;
    Image icon;
    int index = 0;
    bool highlighted = false, isDirectory = false;

    std::unique_ptr<AccessibilityHandler> createAccessibilityHandler() override
    {
        return createIgnoredAccessibilityHandler (*this);
    }

    void updateIcon (const bool onlyUpdateIfCached)
    {
        if (icon.isNull())
        {
            auto hashCode = (file.getFullPathName() + "_iconCacheSalt").hashCode();
            auto im = ImageCache::getFromHashCode (hashCode);

            if (im.isNull() && ! onlyUpdateIfCached)
            {
                im = juce_createIconForFile (file);

                if (im.isValid())
                    ImageCache::addImageToCache (im, hashCode);
            }

            if (im.isValid())
            {
                icon = im;
                triggerAsyncUpdate();
            }
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ItemComponent)
};

//==============================================================================
int FileListComponent::getNumRows()
{
    return directoryContentsList.getNumFiles();
}

String FileListComponent::getNameForRow (int rowNumber)
{
    return directoryContentsList.getFile (rowNumber).getFileName();
}

void FileListComponent::paintListBoxItem (int, Graphics&, int, int, bool)
{
}

Component* FileListComponent::refreshComponentForRow (int row, bool isSelected, Component* existingComponentToUpdate)
{
    jassert (existingComponentToUpdate == nullptr || dynamic_cast<ItemComponent*> (existingComponentToUpdate) != nullptr);

    auto comp = static_cast<ItemComponent*> (existingComponentToUpdate);

    if (comp == nullptr)
        comp = new ItemComponent (*this, directoryContentsList.getTimeSliceThread());

    DirectoryContentsList::FileInfo fileInfo;
    comp->update (directoryContentsList.getDirectory(),
                  directoryContentsList.getFileInfo (row, fileInfo) ? &fileInfo : nullptr,
                  row, isSelected);

    return comp;
}

void FileListComponent::selectedRowsChanged (int /*lastRowSelected*/)
{
    sendSelectionChangeMessage();
}

void FileListComponent::deleteKeyPressed (int /*currentSelectedRow*/)
{
}

void FileListComponent::returnKeyPressed (int currentSelectedRow)
{
    sendDoubleClickMessage (directoryContentsList.getFile (currentSelectedRow));
}

} // namespace juce
