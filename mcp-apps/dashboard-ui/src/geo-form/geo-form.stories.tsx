import type { Meta, StoryObj } from '@storybook/react';
import { GeoFormWidget } from './geo-form';

const meta: Meta<typeof GeoFormWidget> = {
  title: 'MCP UI/Geo Form',
  component: GeoFormWidget,
  parameters: {
    layout: 'centered',
  },
};

export default meta;
type Story = StoryObj<typeof GeoFormWidget>;

export const Default: Story = {};
